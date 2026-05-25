/**
 * @file hcsr04-driver.c
 * @author Aad van Gerwen
 * @brief hcsr04 driver
 * @version 0.1
 * @date 2025-03-18
 **/

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hcsr04_driver.h"

#define TRIGGER_LOW_DELAY 4
#define TRIGGER_HIGH_DELAY 10
#define PING_TIMEOUT 6000
#define ROUNDTRIP_CM 58

#define ESP_HCSR04_TRIGGER_PIN GPIO_NUM_13
#define ESP_HCSR04_ECHO_PIN GPIO_NUM_12

static const char *log_tag = "HCRS04 tag";
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t ultrasonic_measure_raw(uint32_t max_time_us, uint32_t *time_us)
{
    esp_err_t return_value = ESP_OK;
    int64_t echo_start;
    int64_t time = 0;

    if (time_us == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *time_us = 0;

    portENTER_CRITICAL(&mux);
    gpio_set_level(ESP_HCSR04_TRIGGER_PIN, 0);
    esp_rom_delay_us(TRIGGER_LOW_DELAY);
    gpio_set_level(ESP_HCSR04_TRIGGER_PIN, 1);
    esp_rom_delay_us(TRIGGER_HIGH_DELAY);
    gpio_set_level(ESP_HCSR04_TRIGGER_PIN, 0);
    portEXIT_CRITICAL(&mux);

    if (gpio_get_level(ESP_HCSR04_ECHO_PIN)) {
        return_value = ESP_ERR_ULTRASONIC_PING;
    }

    echo_start = esp_timer_get_time();
    while (!gpio_get_level(ESP_HCSR04_ECHO_PIN) && return_value == ESP_OK) {
        time = esp_timer_get_time();
        if (time - echo_start >= PING_TIMEOUT) {
            return_value = ESP_ERR_ULTRASONIC_PING_TIMEOUT;
        }
    }

    echo_start = esp_timer_get_time();
    time = echo_start;
    while (gpio_get_level(ESP_HCSR04_ECHO_PIN) && return_value == ESP_OK) {
        time = esp_timer_get_time();
        if (time - echo_start >= max_time_us) {
            return_value = ESP_ERR_ULTRASONIC_ECHO_TIMEOUT;
        }
    }

    *time_us = time - echo_start;
    return return_value;
}

esp_err_t UltrasonicInit(void)
{
    gpio_reset_pin(ESP_HCSR04_TRIGGER_PIN);
    gpio_reset_pin(ESP_HCSR04_ECHO_PIN);
    gpio_set_direction(ESP_HCSR04_TRIGGER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ESP_HCSR04_ECHO_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ESP_HCSR04_ECHO_PIN, GPIO_PULLDOWN_ONLY);
    gpio_set_level(ESP_HCSR04_TRIGGER_PIN, 0);

    return ESP_OK;
}

esp_err_t UltrasonicMeasure(uint32_t max_distance, uint32_t *distance)
{
    uint32_t time_us;
    esp_err_t return_value = ESP_OK;

    if (distance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return_value = ultrasonic_measure_raw(max_distance * ROUNDTRIP_CM, &time_us);
    *distance = time_us / ROUNDTRIP_CM;
    return return_value;
}

void UltrasonicAssert(esp_err_t error_code)
{
    if (error_code != ESP_OK) {
        ESP_LOGI(log_tag, "Measurement error: %x\n", error_code);
    }
}
