idf_component_register(
  SRCS
    "app_main.c"
    "device_auth.c"
    "http_upload.c"
    "local_queue.c"
    "sensor_reader.c"
    "spiffs_init.c"
    "time_sync.c"
    "wifi_manager.c"
  INCLUDE_DIRS "."
  REQUIRES json esp_http_client esp_wifi esp_netif esp_event spiffs nvs_flash mbedtls driver
)
