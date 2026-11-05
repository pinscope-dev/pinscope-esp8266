# Pinscope as an ESP8266_RTOS_SDK component: the portable core plus the port.
# In the public repository the core is in core/; in Pinscope's own repository, four levels up.
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/core/ps_core.c)
  set(CORE ${CMAKE_CURRENT_LIST_DIR}/core)
else()
  set(CORE ${CMAKE_CURRENT_LIST_DIR}/../../../../device/pinscope/src)
endif()
idf_component_register(
  SRCS ps_port_esp8266.c ${CORE}/ps_core.c ${CORE}/ps_crypto.c
  INCLUDE_DIRS ${CORE}
  REQUIRES lwip nvs_flash app_update tcpip_adapter esp8266 log
)
