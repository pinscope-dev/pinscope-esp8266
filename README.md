# Pinscope for the ESP8266

Remote access, logs, metrics and firmware updates for the ESP8266 with
Espressif's [ESP8266_RTOS_SDK](https://github.com/espressif/ESP8266_RTOS_SDK) 3.4,
over one outbound connection to [Pinscope](https://pinscope.dev). About 7 KB of
RAM, no TLS library.

This repository is a complete example project; the component is
`components/pinscope`.

## Try it in five minutes

With the ESP8266_RTOS_SDK 3.4 and its toolchain set up
([guide](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/)),
and the project key from **Connect a device** in the [dashboard](https://pinscope.dev/#/):

```sh
git clone https://github.com/pinscope-dev/pinscope-esp8266 && cd pinscope-esp8266
export IDF_PATH=~/esp/ESP8266_RTOS_SDK
PINSCOPE_KEY=psk_... WIFI_SSID=... WIFI_PASSWORD=... idf.py build flash monitor
```

Set up for a 4 MB board with two OTA slots (Wemos D1 mini). For an update,
upload `build/pinscope_esp8266.bin` under Firmware.

## Use it in your project

Copy `components/pinscope` into your project's `components/` folder, enter the
key in `idf.py menuconfig` under **Pinscope**, and once WiFi is started:

```c
#include "pinscope.h"

pinscope_start(NULL);
```

The Arduino ESP8266 core is not supported.

Documentation: https://pinscope.dev/docs/esp8266

## Licence

Source-available: the licence in `LICENSE` allows use in any product, including
commercial ones, for connecting to [Pinscope](https://pinscope.dev). Questions:
hello@pinscope.dev.
