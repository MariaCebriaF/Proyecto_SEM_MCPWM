#ifndef CONTROL_H
#define CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Inicializa las colas que usará el control
void control_init(QueueHandle_t cmd_queue, QueueHandle_t mot_queue, QueueHandle_t telemetry_queue);

// Ejecuta la lógica manual (lee Wi-Fi, evita colisión, manda a motores)
void control_manual(void);

// Ejecuta la lógica autónoma (lee sensor, decide giro, manda a motores)
void control_autonomo(void);

#ifdef __cplusplus
}
#endif

#endif // CONTROL_H
