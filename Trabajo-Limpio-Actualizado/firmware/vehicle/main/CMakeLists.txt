idf_component_register(
    SRCS "app_main.c" "wifi.c" "control.c"
    INCLUDE_DIRS "."
    REQUIRES sem_protocol hcsr04 driver esp_driver_ledc esp_wifi esp_event esp_netif esp_http_server nvs_flash esp_timer
)
