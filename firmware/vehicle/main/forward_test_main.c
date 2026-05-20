#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "forward_test";

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

#define TEST_DUTY_PERCENT 70
#define TEST_RUN_MS 3000
#define TEST_STOP_MS 2000

typedef struct {
    gpio_num_t in1_gpio;
    gpio_num_t in2_gpio;
    gpio_num_t pwm_gpio;
    bool inverted;
    mcpwm_cmpr_handle_t comparator;
    mcpwm_gen_handle_t generator;
} motor_channel_t;

static mcpwm_timer_handle_t s_timer;
static mcpwm_oper_handle_t s_operator;
static motor_channel_t s_motor_a = {
    .in1_gpio = TB6612_AIN1_GPIO,
    .in2_gpio = TB6612_AIN2_GPIO,
    .pwm_gpio = TB6612_PWMA_GPIO,
    .inverted = false,
};
static motor_channel_t s_motor_b = {
    .in1_gpio = TB6612_BIN1_GPIO,
    .in2_gpio = TB6612_BIN2_GPIO,
    .pwm_gpio = TB6612_PWMB_GPIO,
    .inverted = false,
};

static esp_err_t motor_set(motor_channel_t *motor, int duty_percent)
{
    if (duty_percent > 100) {
        duty_percent = 100;
    } else if (duty_percent < -100) {
        duty_percent = -100;
    }

    if (motor->inverted) {
        duty_percent = -duty_percent;
    }

    const uint32_t duty_ticks = (abs(duty_percent) * TB6612_PWM_PERIOD_TICKS) / 100;
    ESP_RETURN_ON_ERROR(mcpwm_comparator_set_compare_value(motor->comparator, duty_ticks), TAG,
                        "set compare");

    if (duty_percent > 0) {
        gpio_set_level(motor->in1_gpio, 1);
        gpio_set_level(motor->in2_gpio, 0);
    } else if (duty_percent < 0) {
        gpio_set_level(motor->in1_gpio, 0);
        gpio_set_level(motor->in2_gpio, 1);
    } else {
        gpio_set_level(motor->in1_gpio, 0);
        gpio_set_level(motor->in2_gpio, 0);
    }

    return ESP_OK;
}

static esp_err_t motor_channel_init(motor_channel_t *motor)
{
    gpio_config_t direction_io = {
        .pin_bit_mask = (1ULL << motor->in1_gpio) | (1ULL << motor->in2_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&direction_io), TAG, "configure direction pins");

    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_comparator(s_operator, &comparator_config, &motor->comparator),
                        TAG, "create comparator");

    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = motor->pwm_gpio,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_generator(s_operator, &generator_config, &motor->generator), TAG,
                        "create generator");

    ESP_RETURN_ON_ERROR(mcpwm_generator_set_actions_on_timer_event(
                            motor->generator,
                            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                                         MCPWM_TIMER_EVENT_EMPTY,
                                                         MCPWM_GEN_ACTION_HIGH),
                            MCPWM_GEN_TIMER_EVENT_ACTION_END()),
                        TAG, "set timer action");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_actions_on_compare_event(
                            motor->generator,
                            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                                           motor->comparator,
                                                           MCPWM_GEN_ACTION_LOW),
                            MCPWM_GEN_COMPARE_EVENT_ACTION_END()),
                        TAG, "set compare action");

    return motor_set(motor, 0);
}

static esp_err_t motors_init(void)
{
    gpio_config_t standby_io = {
        .pin_bit_mask = 1ULL << TB6612_STBY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&standby_io), TAG, "configure STBY");
    gpio_set_level(TB6612_STBY_GPIO, 0);

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = TB6612_PWM_RESOLUTION_HZ,
        .period_ticks = TB6612_PWM_PERIOD_TICKS,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_timer(&timer_config, &s_timer), TAG, "create timer");

    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_operator(&operator_config, &s_operator), TAG, "create operator");
    ESP_RETURN_ON_ERROR(mcpwm_operator_connect_timer(s_operator, s_timer), TAG,
                        "connect timer");

    ESP_RETURN_ON_ERROR(motor_channel_init(&s_motor_a), TAG, "init motor A");
    ESP_RETURN_ON_ERROR(motor_channel_init(&s_motor_b), TAG, "init motor B");

    ESP_RETURN_ON_ERROR(mcpwm_timer_enable(s_timer), TAG, "enable timer");
    ESP_RETURN_ON_ERROR(mcpwm_timer_start_stop(s_timer, MCPWM_TIMER_START_NO_STOP), TAG,
                        "start timer");

    gpio_set_level(TB6612_STBY_GPIO, 1);
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(motors_init());
    ESP_LOGI(TAG, "Forward test ready. Motors will run at %d%% duty.", TEST_DUTY_PERCENT);

    while (true) {
        ESP_LOGI(TAG, "Motor A forward");
        ESP_ERROR_CHECK(motor_set(&s_motor_a, TEST_DUTY_PERCENT));
        ESP_ERROR_CHECK(motor_set(&s_motor_b, 0));
        vTaskDelay(pdMS_TO_TICKS(TEST_RUN_MS));

        ESP_LOGI(TAG, "Stop");
        ESP_ERROR_CHECK(motor_set(&s_motor_a, 0));
        ESP_ERROR_CHECK(motor_set(&s_motor_b, 0));
        vTaskDelay(pdMS_TO_TICKS(TEST_STOP_MS));

        ESP_LOGI(TAG, "Motor B forward");
        ESP_ERROR_CHECK(motor_set(&s_motor_a, 0));
        ESP_ERROR_CHECK(motor_set(&s_motor_b, TEST_DUTY_PERCENT));
        vTaskDelay(pdMS_TO_TICKS(TEST_RUN_MS));

        ESP_LOGI(TAG, "Stop");
        ESP_ERROR_CHECK(motor_set(&s_motor_a, 0));
        ESP_ERROR_CHECK(motor_set(&s_motor_b, 0));
        vTaskDelay(pdMS_TO_TICKS(TEST_STOP_MS));
    }
}
