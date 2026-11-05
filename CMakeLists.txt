# Pinscope on an ESP8266 with Espressif's ESP8266_RTOS_SDK v3.4:
#   export IDF_PATH=<ESP8266_RTOS_SDK>
#   PINSCOPE_KEY=psk_... WIFI_SSID=... WIFI_PASSWORD=... idf.py build flash monitor
cmake_minimum_required(VERSION 3.5)
# The SDK's own components declare CMake versions that CMake 4 no longer accepts.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pinscope_esp8266)
