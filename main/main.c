/*
 * Pinscope on an ESP8266: a small web page on port 80, reachable through
 * the device's Pinscope address, plus metrics and logs.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"

#include "pinscope.h"

static void serve(void *arg)
{
    (void)arg;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(80) };
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(server, 2) != 0) {
        printf("cannot listen on port 80\n");
        vTaskDelete(NULL);
    }
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            continue;
        }
        char request[1024], page[192], head[128];
        /* Read the whole request: closing with unread bytes resets the
         * connection, and the reply would be lost. */
        size_t got = 0;
        while (got < sizeof(request) - 1) {
            int n = (int)recv(client, request + got, sizeof(request) - 1 - got, 0);
            if (n <= 0) {
                break;
            }
            got += (size_t)n;
            request[got] = '\0';
            if (strstr(request, "\r\n\r\n")) {
                break;
            }
        }
        int body = snprintf(page, sizeof(page), "<!doctype html><title>ESP8266</title><h1>Hello from an ESP8266</h1><p>Up for %lu s, %u bytes free.</p>",
                            (unsigned long)(esp_timer_get_time() / 1000000), (unsigned)esp_get_free_heap_size());
        int n = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", body);
        send(client, head, n, 0);
        send(client, page, body, 0);
        close(client);
    }
}

void app_main(void)
{
    nvs_flash_init();
    tcpip_adapter_init();
    esp_event_loop_create_default();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init);
    wifi_config_t sta = { 0 };
    strncpy((char *)sta.sta.ssid, WIFI_SSID, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, WIFI_PASSWORD, sizeof(sta.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &sta);
    esp_wifi_start();
    esp_wifi_connect();

    /* The key: PINSCOPE_KEY in the environment when building, else menuconfig (Pinscope). */
    pinscope_config_t config = { .project_key = PINSCOPE_KEY[0] ? PINSCOPE_KEY : NULL };
    pinscope_start(&config);
    xTaskCreate(serve, "web", 3072, NULL, 4, NULL);
    printf("free heap with Pinscope running: %u\n", (unsigned)esp_get_free_heap_size());
}
