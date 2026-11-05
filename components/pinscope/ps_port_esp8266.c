/* SPDX-License-Identifier: LicenseRef-Pinscope-Device-1.0
 * Copyright (c) 2026 Pinscope. See LICENSE: for use with the Pinscope service only. */
/*
 * Pinscope platform layer for the ESP8266 with ESP8266_RTOS_SDK: FreeRTOS,
 * lwIP sockets, NVS and the SDK's two OTA slots. The SDK has no rollback, so
 * an update is not tried first. Internal, not in the public component.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#include "pinscope.h"
#include "ps_port.h"

static const char *TAG = "pinscope";

static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_task;
static bool              s_capture;

int ps_port_init(void)
{
    if (s_lock) {
        return 0;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? 0 : -1;
}

uint32_t ps_port_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void     ps_port_sleep(uint32_t ms) { vTaskDelay(ms / portTICK_PERIOD_MS ? ms / portTICK_PERIOD_MS : 1); }

/* The hardware generator; its output is random while the radio is on, which
 * it is whenever Pinscope connects. */
void ps_port_random(void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len) {
        uint32_t w = esp_random();
        size_t n = len < 4 ? len : 4;
        memcpy(p, &w, n);
        p += n;
        len -= n;
    }
}

void ps_port_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
void ps_port_unlock(void) { xSemaphoreGive(s_lock); }

int ps_port_task_start(void (*fn)(void *), uint32_t stack, uint8_t priority)
{
    return xTaskCreate(fn, "pinscope", stack / sizeof(StackType_t), NULL, priority, &s_task) == pdPASS ? 0 : -1;
}

void ps_port_logf(int level, const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (level == PS_LOG_ERROR) {
        ESP_LOGE(TAG, "%s", line);
    } else if (level == PS_LOG_WARN) {
        ESP_LOGW(TAG, "%s", line);
    } else {
        ESP_LOGI(TAG, "%s", line);
    }
    if (s_capture) {
        char out[144];
        int n = snprintf(out, sizeof(out), "%c pinscope: %s\n", "EWI"[level], line);
        pinscope_log(out, n < (int)sizeof(out) ? (size_t)n : sizeof(out) - 1);
    }
}

void ps_port_device_id(char *buf, size_t len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, len, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static const char *reset_name(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_SW:       return "sw";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT:      return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_EXT:      return "ext";
    default:               return "other";
    }
}

/* A new value on every start: the server counts a changed one as a restart. */
static uint32_t boot_id(void)
{
    static uint32_t id;
    if (!id) {
        ps_port_random(&id, sizeof(id));
        id |= 1;
    }
    return id;
}

void ps_port_info(char *buf, size_t len)
{
    const esp_app_desc_t *app = esp_ota_get_app_description();
    snprintf(buf, len, "chip=esp8266;fw=%.31s;idf=%.15s;reset=%s;boot=%08x", app ? app->version : "", esp_get_idf_version(),
             reset_name(), (unsigned)boot_id());
}

int ps_port_kv_get(const char *key, char *buf, size_t len)
{
    nvs_handle h;
    if (nvs_open("pinscope", NVS_READONLY, &h) != ESP_OK) {
        return -1;
    }
    esp_err_t err = nvs_get_str(h, key, buf, &len);
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}

int ps_port_kv_set(const char *key, const char *value)
{
    nvs_handle h;
    if (nvs_open("pinscope", NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    esp_err_t err = nvs_set_str(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}

void ps_port_log_capture(bool on) { s_capture = on; }

/* The reset reason says when there was a crash; the SDK keeps no backtrace. */
int  ps_port_crash_read(char *buf, size_t len) { (void)buf; (void)len; return 0; }
void ps_port_crash_clear(void) {}

/* ---- updates: the SDK's spare OTA slot ---- */

static esp_ota_handle_t       s_ota;
static const esp_partition_t *s_ota_part;

bool ps_port_ota_supported(void) { return esp_ota_get_next_update_partition(NULL) != NULL; }

int ps_port_ota_begin(uint32_t size)
{
    s_ota_part = esp_ota_get_next_update_partition(NULL);
    if (!s_ota_part || size > s_ota_part->size) {
        return -1;
    }
    return esp_ota_begin(s_ota_part, size, &s_ota) == ESP_OK ? 0 : -1;
}

int ps_port_ota_write(const void *data, size_t len)
{
    return esp_ota_write(s_ota, data, len) == ESP_OK ? 0 : -1;
}

int ps_port_ota_end(void)
{
    if (esp_ota_end(s_ota) != ESP_OK) {
        return -1;
    }
    return esp_ota_set_boot_partition(s_ota_part) == ESP_OK ? 0 : -1;
}

void ps_port_ota_abort(void)
{
    if (s_ota) {
        esp_ota_end(s_ota);
        s_ota = 0;
    }
}

void ps_port_ota_confirm(void) {}
bool ps_port_ota_on_trial(void) { return false; }
void ps_port_restart(void) { esp_restart(); }

/* ---- system metrics ---- */

uint16_t ps_port_default_metrics(void)
{
    return PINSCOPE_M_HEAP | PINSCOPE_M_UPTIME | PINSCOPE_M_RSSI | PINSCOPE_M_STACK;
}

void ps_port_sys_metrics(uint16_t mask)
{
    if (mask & PINSCOPE_M_UPTIME) {
        pinscope_metric("uptime_s", (double)(esp_timer_get_time() / 1000000));
    }
    if (mask & PINSCOPE_M_HEAP) {
        pinscope_metric("heap_free", (double)esp_get_free_heap_size());
        pinscope_metric("heap_min", (double)esp_get_minimum_free_heap_size());
    }
    if (mask & PINSCOPE_M_STACK) {
        pinscope_metric("stack/pinscope", (double)(uxTaskGetStackHighWaterMark(s_task) * sizeof(StackType_t)));
    }
    wifi_ap_record_t ap;
    if ((mask & PINSCOPE_M_RSSI) && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        pinscope_metric("rssi", (double)ap.rssi);
    }
}

/* ---- connections to local servers: lwIP sockets ---- */

int ps_local_open(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    ps_set_nonblock(fd);

    /* The device's own address, which lwIP loops back (LWIP_NETIF_LOOPBACK,
     * on by default); 127.0.0.1 would need a loopback interface as well. */
    struct sockaddr_in addr;
    tcpip_adapter_ip_info_t ip;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip) == ESP_OK && ip.ip.addr
                               ? ip.ip.addr : htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 && !ps_sock_inprogress()) {
        close(fd);
        return -1;
    }
    return fd;
}

int ps_local_state(int h)
{
    fd_set w;
    struct timeval tv = { 0, 0 };
    int err = 0;
    socklen_t len = sizeof(err);
    FD_ZERO(&w);
    FD_SET(h, &w);
    if (select(h + 1, NULL, &w, NULL, &tv) <= 0) {
        return 0;
    }
    getsockopt(h, SOL_SOCKET, SO_ERROR, &err, &len);
    return err == 0 ? 1 : -1;
}

int ps_local_read(int h, void *buf, size_t len)
{
    int n = (int)recv(h, buf, len, 0);
    if (n > 0) {
        return n;
    }
    return (n < 0 && ps_sock_wouldblock()) ? 0 : -1;
}

int ps_local_write(int h, const void *buf, size_t len)
{
    int n = (int)send(h, buf, len, 0);
    if (n > 0) {
        return n;
    }
    return (n < 0 && ps_sock_wouldblock()) ? 0 : -1;
}

int  ps_local_avail(int h) { (void)h; return -1; }
void ps_local_want(int h, size_t bytes) { (void)h; (void)bytes; }
void ps_local_close(int h) { close(h); }

void ps_local_watch(int h, bool connecting, bool want_read, bool want_write,
                    fd_set *r, fd_set *w, fd_set *e, int *maxfd)
{
    if (connecting) {
        FD_SET(h, w);
        FD_SET(h, e);
    }
    if (want_read) {
        FD_SET(h, r);
    }
    if (want_write) {
        FD_SET(h, w);
    }
    if (h > *maxfd) {
        *maxfd = h;
    }
}

void ps_local_flush(void) {}
bool ps_local_ready(void) { return false; }
void ps_local_poll(const fd_set *readable) { (void)readable; }

bool ps_port_network_ready(void)
{
    tcpip_adapter_ip_info_t ip;
    return tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip) == ESP_OK && ip.ip.addr != 0;
}

int ps_port_resolve(const char *host, uint8_t *scratch, size_t len, uint32_t *addr)
{
    struct addrinfo hints, *res = NULL;
    (void)scratch;
    (void)len;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
        return -1;
    }
    *addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
    return 0;
}
