#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "sem_protocol.h"

static const char *TAG = "sem_remote";

static QueueHandle_t s_command_queue;

static int16_t read_joystick_axis_stub(void)
{
    return 0;
}

static void joystick_task(void *arg)
{
    (void)arg;
    sem_control_command_t command = sem_protocol_default_command();
    command.enable = true;

    while (true) {
        command.throttle = read_joystick_axis_stub();
        command.steering = read_joystick_axis_stub();
        command.sequence++;

        xQueueOverwrite(s_command_queue, &command);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void communication_task(void *arg)
{
    (void)arg;
    sem_control_command_t command = sem_protocol_default_command();

    while (true) {
        if (xQueueReceive(s_command_queue, &command, pdMS_TO_TICKS(200)) == pdTRUE) {
            /* TODO: replace with WiFi send path. */
            ESP_LOGI(TAG, "send throttle=%d steering=%d seq=%lu",
                     command.throttle,
                     command.steering,
                     (unsigned long)command.sequence);
        }
    }
}

void app_main(void)
{
    s_command_queue = xQueueCreate(1, sizeof(sem_control_command_t));

    xTaskCreate(joystick_task, "joystick", 4096, NULL, 5, NULL);
    xTaskCreate(communication_task, "communication", 4096, NULL, 4, NULL);
}
