#include "control.h"
#include <stdlib.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "sem_protocol.h"

//static const char *TAG = "CONTROL_MOD";

/* ── Punteros locales a las colas ── */
static QueueHandle_t q_cmd_in = NULL;
static QueueHandle_t q_mot_out = NULL;
static QueueHandle_t q_tel_out = NULL;

/* ── Variables Globales Externas ── */
extern portMUX_TYPE spinlock_ultrasonidos;
extern volatile uint16_t g_distancia_cm;
/* ── Umbrales ── */
#define DIST_PELIGRO_CM     20u
#define DIST_PRECAUCION_CM  40u
#define COMMAND_TIMEOUT_US  500000LL

/* ── Velocidades y Dirección (Escala -1000 a 1000) ── */
#define VEL_RAPIDA   800
#define VEL_NORMAL   600
#define VEL_LENTA    350
#define VEL_PARADO   0

#define DIRECCION_CENTRO 0
#define DIRECCION_IZQ   -1000
#define DIRECCION_DER    1000

/* ──────────────────────────────────────────────────────── */
void control_init(QueueHandle_t cmd_queue, QueueHandle_t mot_queue, QueueHandle_t telemetry_queue)
{
    q_cmd_in = cmd_queue;
    q_mot_out = mot_queue;
    q_tel_out = telemetry_queue;
}

/* ──────────────────────────────────────────────────────── */
void control_manual(void)
{
    if (q_cmd_in == NULL || q_mot_out == NULL) return;

    static sem_control_command_t ultimo_comando;
    static int64_t ultimo_comando_us = 0;
    static bool inicializado = false;

    if (!inicializado) {
        ultimo_comando = sem_protocol_default_command();
        inicializado = true;
    }

    /* 1. Leer comandos de Wi-Fi. Si no hay nada nuevo, conserva el último. */
    sem_control_command_t comando_recibido;
    if (xQueueReceive(q_cmd_in, &comando_recibido, 0) == pdTRUE &&
        comando_recibido.version == SEM_PROTOCOL_VERSION) {
        ultimo_comando = comando_recibido;
        ultimo_comando_us = esp_timer_get_time();
    }

    const int64_t ahora_us = esp_timer_get_time();
    const bool link_ok = ultimo_comando.enable &&
                         ultimo_comando_us > 0 &&
                         (ahora_us - ultimo_comando_us) <= COMMAND_TIMEOUT_US;

    int16_t out_throttle = link_ok ? ultimo_comando.throttle : 0;
    int16_t out_steering = link_ok ? ultimo_comando.steering : 0;

    /* 2. Frenada de seguridad con ultrasonido */
    uint16_t dist;
    taskENTER_CRITICAL(&spinlock_ultrasonidos);
        dist = g_distancia_cm;
    taskEXIT_CRITICAL(&spinlock_ultrasonidos);

    if (out_throttle > 0) {
        out_throttle = sem_protocol_limit_throttle_by_distance(out_throttle, dist);
    }

    /* 3. Enviar a motores/telemetría */
    sem_vehicle_telemetry_t paquete = sem_protocol_default_telemetry();
    paquete.applied_throttle = out_throttle;
    paquete.applied_steering = out_steering;
    paquete.distance_cm = dist;
    paquete.state = sem_protocol_state_from_distance(dist, link_ok);
    paquete.last_command_sequence = ultimo_comando.sequence;
    xQueueOverwrite(q_mot_out, &paquete);
    if (q_tel_out != NULL) {
        xQueueOverwrite(q_tel_out, &paquete);
    }
}

/* ──────────────────────────────────────────────────────── */
void control_autonomo(void)
{
    if (q_mot_out == NULL) return;

    static uint8_t ultimo_giro = 0; 
    int16_t out_throttle = 0;
    int16_t out_steering = 0;

    uint16_t dist;
    taskENTER_CRITICAL(&spinlock_ultrasonidos);
        dist = g_distancia_cm;
    taskEXIT_CRITICAL(&spinlock_ultrasonidos);

    if (dist > DIST_PRECAUCION_CM) {
        out_throttle = VEL_RAPIDA;
        out_steering = DIRECCION_CENTRO;
    }
    else if (dist > DIST_PELIGRO_CM) {
        out_throttle = VEL_LENTA;
        out_steering = DIRECCION_CENTRO;
    }
    else {
        out_throttle = -VEL_LENTA; /* Frena/Marcha atrás lenta */
        if(ultimo_giro == 0) {
            out_steering = DIRECCION_IZQ;
            ultimo_giro = 1;
        } else {
            out_steering = DIRECCION_DER;
            ultimo_giro = 0;
        }
    }

    /* Enviar decisiones a motores y servo */
    sem_vehicle_telemetry_t paquete = sem_protocol_default_telemetry();
    paquete.applied_throttle = out_throttle;
    paquete.applied_steering = out_steering;
    paquete.distance_cm = dist;
    paquete.state = sem_protocol_state_from_distance(dist, true);
    xQueueOverwrite(q_mot_out, &paquete);
    if (q_tel_out != NULL) {
        xQueueOverwrite(q_tel_out, &paquete);
    }
}
