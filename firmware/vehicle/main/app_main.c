#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "sem_protocol.h"

static const char *TAG = "sem_vehicle";

static QueueHandle_t s_command_queue;
static QueueHandle_t s_telemetry_queue;

static uint16_t read_distance_cm_stub(void)
{
    return 100;
}

static void apply_motor_output_stub(int16_t throttle, int16_t steering)
{
    ESP_LOGI(TAG, "motor throttle=%d steering=%d", throttle, steering);
}

static void communication_task(void *arg)
{
    (void)arg;
    sem_control_command_t command = sem_protocol_default_command();
    command.enable = true;

    while (true) {
        /* TODO: replace with WiFi receive path. */
        command.sequence++;
        xQueueOverwrite(s_command_queue, &command);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void control_task(void *arg)
{
    (void)arg;
    sem_control_command_t command = sem_protocol_default_command();
    sem_vehicle_telemetry_t telemetry = sem_protocol_default_telemetry();

    while (true) {
        xQueueReceive(s_command_queue, &command, 0);

        const uint16_t distance_cm = read_distance_cm_stub();
        const bool link_ok = true; /* TODO: derive from command timeout. */
        int16_t throttle = command.enable ? command.throttle : 0;

        throttle = sem_protocol_limit_throttle_by_distance(throttle, distance_cm);

        telemetry.applied_throttle = throttle;
        telemetry.applied_steering = command.steering;
        telemetry.distance_cm = distance_cm;
        telemetry.state = sem_protocol_state_from_distance(distance_cm, link_ok);
        telemetry.last_command_sequence = command.sequence;

        apply_motor_output_stub(telemetry.applied_throttle, telemetry.applied_steering);
        xQueueOverwrite(s_telemetry_queue, &telemetry);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void telemetry_task(void *arg)
{
    (void)arg;
    sem_vehicle_telemetry_t telemetry = sem_protocol_default_telemetry();

    while (true) {
        if (xQueueReceive(s_telemetry_queue, &telemetry, pdMS_TO_TICKS(500)) == pdTRUE) {
            ESP_LOGI(TAG, "distance=%u state=%d seq=%lu",
                     telemetry.distance_cm,
                     telemetry.state,
                     (unsigned long)telemetry.last_command_sequence);
        }
    }
}

void app_main(void)
{
    s_command_queue = xQueueCreate(1, sizeof(sem_control_command_t));
    s_telemetry_queue = xQueueCreate(1, sizeof(sem_vehicle_telemetry_t));

    xTaskCreate(communication_task, "communication", 4096, NULL, 5, NULL);
    xTaskCreate(control_task, "control", 4096, NULL, 6, NULL);
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, NULL);
}
