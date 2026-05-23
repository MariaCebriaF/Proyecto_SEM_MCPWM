#include "wifi.h"
#include "control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
//#include "esp_http_server.h"

#include "sem_protocol.h"

static const char *TAG = "sem_vehicle";

// --------------------- VARIABLES GLOBALES ---------------------
volatile uint8_t g_modo = 0; // 0 = manual, 1 = autónomo
portMUX_TYPE spinlock_modo = portMUX_INITIALIZER_UNLOCKED;

volatile uint16_t g_distancia_cm = 100; //Valor seguro por defecto
portMUX_TYPE spinlock_ultrasonidos = portMUX_INITIALIZER_UNLOCKED;



#define SEM_WIFI_SSID "SEM-PICAR"
#define SEM_WIFI_CHANNEL 1
#define SEM_WIFI_MAX_CLIENTS 4

#define SEM_COMMAND_TIMEOUT_MS 500
#define SEM_CONTROL_PERIOD_MS 50

#define TB6612_PWM_RESOLUTION_HZ 1000000
#define TB6612_PWM_FREQ_HZ 20000
#define TB6612_PWM_PERIOD_TICKS (TB6612_PWM_RESOLUTION_HZ / TB6612_PWM_FREQ_HZ)


#define TB6612_STBY_GPIO GPIO_NUM_17
#define TB6612_AIN1_GPIO GPIO_NUM_5
#define TB6612_AIN2_GPIO GPIO_NUM_6
#define TB6612_PWMA_GPIO GPIO_NUM_4
#define TB6612_BIN1_GPIO GPIO_NUM_15
#define TB6612_BIN2_GPIO GPIO_NUM_16
#define TB6612_PWMB_GPIO GPIO_NUM_7

typedef struct {
    gpio_num_t in1_gpio;
    gpio_num_t in2_gpio;
    gpio_num_t pwm_gpio;
    bool inverted;
    mcpwm_cmpr_handle_t comparator;
    mcpwm_gen_handle_t generator;
} tb6612_channel_t;

typedef struct {
    gpio_num_t standby_gpio;
    tb6612_channel_t left;
    tb6612_channel_t right;
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t operator;
} tb6612_driver_t;

static QueueHandle_t s_command_queue;
static QueueHandle_t s_telemetry_queue;
static QueueHandle_t s_motor_queue;
static tb6612_driver_t s_motor_driver = {
    .standby_gpio = TB6612_STBY_GPIO,
    .left = {
        .in1_gpio = TB6612_AIN1_GPIO,
        .in2_gpio = TB6612_AIN2_GPIO,
        .pwm_gpio = TB6612_PWMA_GPIO,
        .inverted = false,
    },
    .right = {
        .in1_gpio = TB6612_BIN1_GPIO,
        .in2_gpio = TB6612_BIN2_GPIO,
        .pwm_gpio = TB6612_PWMB_GPIO,
        .inverted = true,
    },
};

/*
static uint16_t read_distance_cm_stub(void)
{
    return 100;
}
*/

static esp_err_t tb6612_set_channel(tb6612_channel_t *channel, int16_t speed)
{
    speed = sem_protocol_clamp_axis(speed);

    if (channel->inverted) {
        speed = -speed;
    }

    const uint32_t duty_ticks = (abs(speed) * TB6612_PWM_PERIOD_TICKS) / 1000;
    ESP_RETURN_ON_ERROR(mcpwm_comparator_set_compare_value(channel->comparator, duty_ticks), TAG,
                        "set PWM compare");

    if (speed > 0) {
        gpio_set_level(channel->in1_gpio, 1);
        gpio_set_level(channel->in2_gpio, 0);
    } else if (speed < 0) {
        gpio_set_level(channel->in1_gpio, 0);
        gpio_set_level(channel->in2_gpio, 1);
    } else {
        gpio_set_level(channel->in1_gpio, 0);
        gpio_set_level(channel->in2_gpio, 0);
    }

    return ESP_OK;
}

static esp_err_t tb6612_init_channel(tb6612_driver_t *driver, tb6612_channel_t *channel)
{
    gpio_config_t direction_io = {
        .pin_bit_mask = (1ULL << channel->in1_gpio) | (1ULL << channel->in2_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&direction_io), TAG, "configure direction GPIO");

    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_comparator(driver->operator, &comparator_config,
                                             &channel->comparator),
                        TAG, "create MCPWM comparator");

    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = channel->pwm_gpio,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_generator(driver->operator, &generator_config, &channel->generator),
                        TAG, "create MCPWM generator");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_actions_on_timer_event(
                            channel->generator,
                            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                                         MCPWM_TIMER_EVENT_EMPTY,
                                                         MCPWM_GEN_ACTION_HIGH),
                            MCPWM_GEN_TIMER_EVENT_ACTION_END()),
                        TAG, "set MCPWM timer action");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_actions_on_compare_event(
                            channel->generator,
                            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                                           channel->comparator,
                                                           MCPWM_GEN_ACTION_LOW),
                            MCPWM_GEN_COMPARE_EVENT_ACTION_END()),
                        TAG, "set MCPWM compare action");

    return tb6612_set_channel(channel, 0);
}

static esp_err_t tb6612_init(tb6612_driver_t *driver)
{
    gpio_config_t standby_io = {
        .pin_bit_mask = 1ULL << driver->standby_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&standby_io), TAG, "configure STBY GPIO");
    gpio_set_level(driver->standby_gpio, 0);

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = TB6612_PWM_RESOLUTION_HZ,
        .period_ticks = TB6612_PWM_PERIOD_TICKS,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_timer(&timer_config, &driver->timer), TAG, "create MCPWM timer");

    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_operator(&operator_config, &driver->operator), TAG,
                        "create MCPWM operator");
    ESP_RETURN_ON_ERROR(mcpwm_operator_connect_timer(driver->operator, driver->timer), TAG,
                        "connect MCPWM timer");
    ESP_RETURN_ON_ERROR(tb6612_init_channel(driver, &driver->left), TAG, "init left motor");
    ESP_RETURN_ON_ERROR(tb6612_init_channel(driver, &driver->right), TAG, "init right motor");
    ESP_RETURN_ON_ERROR(mcpwm_timer_enable(driver->timer), TAG, "enable MCPWM timer");
    ESP_RETURN_ON_ERROR(mcpwm_timer_start_stop(driver->timer, MCPWM_TIMER_START_NO_STOP), TAG,
                        "start MCPWM timer");

    gpio_set_level(driver->standby_gpio, 1);
    return ESP_OK;
}

static esp_err_t tb6612_drive(tb6612_driver_t *driver, int16_t throttle, int16_t steering)
{
    const int16_t left = sem_protocol_clamp_axis((int32_t)throttle + steering);
    const int16_t right = sem_protocol_clamp_axis((int32_t)throttle - steering);

    ESP_RETURN_ON_ERROR(tb6612_set_channel(&driver->left, left), TAG, "drive left motor");
    ESP_RETURN_ON_ERROR(tb6612_set_channel(&driver->right, right), TAG, "drive right motor");
    return ESP_OK;
}


// --------------------------- TAREAS --------------------------


static void control_task(void *arg)
{
    TickType_t xUltimoTick = xTaskGetTickCount();

    for (;;) {
        // Leer el modo actual
        uint8_t modo_actual;
        taskENTER_CRITICAL(&spinlock_modo);
            modo_actual = g_modo;
        taskEXIT_CRITICAL(&spinlock_modo);

        // Decidir qué función ejecutar
        if (modo_actual == 0) {
            control_manual();
        } else {
            control_autonomo();
        }

        vTaskDelayUntil(&xUltimoTick, pdMS_TO_TICKS(50));
    }
}

static void motor_task(void *arg)
{
    (void)arg;
    sem_vehicle_telemetry_t telemetry = sem_protocol_default_telemetry();

    while (true) {
        if (xQueueReceive(s_motor_queue, &telemetry, portMAX_DELAY) == pdTRUE) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(tb6612_drive(&s_motor_driver,
                                                       telemetry.applied_throttle,
                                                       telemetry.applied_steering));
        }
    }
}

static void telemetry_task(void *arg)
{
    (void)arg;
    sem_vehicle_telemetry_t telemetry = sem_protocol_default_telemetry();

    while (true) {
        if (xQueuePeek(s_telemetry_queue, &telemetry, pdMS_TO_TICKS(500)) == pdTRUE) {
            ESP_LOGI(TAG, "distance=%u state=%d seq=%lu",
                     telemetry.distance_cm,
                     telemetry.state,
                     (unsigned long)telemetry.last_command_sequence);
        }
    }
}

// --------------------------- FIN TAREAS --------------------------


void app_main(void)
{
    s_command_queue = xQueueCreate(1, sizeof(sem_control_command_t));
    s_telemetry_queue = xQueueCreate(1, sizeof(sem_vehicle_telemetry_t));
    s_motor_queue = xQueueCreate(1, sizeof(sem_vehicle_telemetry_t));

    ESP_ERROR_CHECK(tb6612_init(&s_motor_driver));
    //ESP_ERROR_CHECK(wifi_start_ap());
    //ESP_ERROR_CHECK(start_web_server());

    ESP_ERROR_CHECK(wifi_service_init(s_command_queue));

    control_init(s_command_queue, s_motor_queue);

    xTaskCreate(control_task, "control", 4096, NULL, 6, NULL);
    xTaskCreate(motor_task, "motor", 4096, NULL, 5, NULL);
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, NULL);
}
