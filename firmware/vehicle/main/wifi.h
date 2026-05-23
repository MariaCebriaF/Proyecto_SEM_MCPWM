#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

/**
 * @brief Inicialización de la conexión Wifi en modo Estación (STA) y servidor WebSocket. 
 * El command_queue -> Manejador de la cola donde se enviarán los comandos recibidos por red
 * @return esp_err_t -> ESP_OK si todo ha ido bien. 
 */


 esp_err_t wifi_service_init(QueueHandle_t command_queue);

 #ifdef __cplusplus
}
#endif

#endif // WIFI_H
