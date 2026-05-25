idf_component_register(
    SRCS "hcsr04_driver.c" "app_main.c" "wifi.c" "control.c"
    INCLUDE_DIRS "."
    REQUIRES sem_protocol esp_driver_ledc esp_wifi esp_event esp_netif esp_http_server nvs_flash esp_timer driver
)
