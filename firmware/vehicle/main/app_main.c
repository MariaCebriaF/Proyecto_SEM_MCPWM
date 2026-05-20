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
#include "esp_http_server.h"

#include "sem_protocol.h"

static const char *TAG = "sem_vehicle";

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

static uint16_t read_distance_cm_stub(void)
{
    return 100;
}

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

static const char s_index_html[] =
    "<!doctype html><html lang=\"es\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>SEM PiCar</title><style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#101820;color:#eef3f7}"
    "main{max-width:760px;margin:0 auto;padding:24px}"
    "h1{font-size:28px;margin:0 0 18px}.panel{border:1px solid #314253;border-radius:8px;padding:18px;background:#172433}"
    ".grid{display:grid;grid-template-columns:repeat(3,72px);gap:10px;justify-content:center;margin:20px 0}"
    "kbd{display:grid;place-items:center;height:58px;border:1px solid #466078;border-radius:8px;background:#223447;font-size:22px}"
    ".active{background:#2d7dd2;border-color:#6bb6ff}.row{display:flex;gap:16px;flex-wrap:wrap}"
    ".metric{flex:1;min-width:160px;padding:12px;border-radius:8px;background:#203044}"
    "button{height:42px;border:0;border-radius:8px;padding:0 16px;background:#e84855;color:white;font-weight:700}"
    "</style></head><body><main><h1>SEM PiCar</h1><section class=\"panel\">"
    "<div class=\"grid\"><span></span><kbd id=\"up\">W</kbd><span></span><kbd id=\"left\">A</kbd><kbd id=\"down\">S</kbd><kbd id=\"right\">D</kbd></div>"
    "<button id=\"stop\">STOP</button><p>Usa WASD o flechas. Espacio desactiva movimiento.</p>"
    "<div class=\"row\"><div class=\"metric\">Throttle <strong id=\"thr\">0</strong></div>"
    "<div class=\"metric\">Steering <strong id=\"ste\">0</strong></div>"
    "<div class=\"metric\">Distancia <strong id=\"dist\">--</strong> cm</div>"
    "<div class=\"metric\">Estado <strong id=\"state\">--</strong></div></div></section></main>"
    "<script>"
    "const keys=new Set();let seq=0;"
    "const map={ArrowUp:'up',w:'up',W:'up',ArrowDown:'down',s:'down',S:'down',ArrowLeft:'left',a:'left',A:'left',ArrowRight:'right',d:'right',D:'right'};"
    "function axes(){let t=0,s=0;if(keys.has('up'))t+=700;if(keys.has('down'))t-=700;if(keys.has('left'))s-=450;if(keys.has('right'))s+=450;return{t,s};}"
    "async function send(enable=true){const a=axes();thr.textContent=a.t;ste.textContent=a.s;"
    "await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`throttle=${a.t}&steering=${a.s}&enable=${enable?1:0}&sequence=${++seq}`}).catch(()=>{});}"
    "function paint(){for(const id of ['up','down','left','right'])document.getElementById(id).classList.toggle('active',keys.has(id));}"
    "addEventListener('keydown',e=>{if(e.code==='Space'){keys.clear();paint();send(false);return;}const k=map[e.key];if(k){e.preventDefault();keys.add(k);paint();send();}});"
    "addEventListener('keyup',e=>{const k=map[e.key];if(k){e.preventDefault();keys.delete(k);paint();send();}});"
    "stop.onclick=()=>{keys.clear();paint();send(false)};setInterval(()=>send(keys.size>0),120);"
    "setInterval(async()=>{const r=await fetch('/api/telemetry').catch(()=>null);if(!r)return;const j=await r.json();dist.textContent=j.distance_cm;state.textContent=j.state;},500);"
    "</script></body></html>";

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static int32_t form_get_i32(const char *body, const char *key, int32_t fallback)
{
    char value[16] = {0};
    if (httpd_query_key_value(body, key, value, sizeof(value)) != ESP_OK) {
        return fallback;
    }
    return strtol(value, NULL, 10);
}

static esp_err_t control_post_handler(httpd_req_t *req)
{
    char body[128] = {0};
    int received = 0;
    const int expected = req->content_len < (sizeof(body) - 1) ? req->content_len : (sizeof(body) - 1);

    while (received < expected) {
        const int chunk = httpd_req_recv(req, body + received, expected - received);
        if (chunk <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
        }
        received += chunk;
    }

    if (received == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }

    sem_control_command_t command = {
        .version = SEM_PROTOCOL_VERSION,
        .throttle = sem_protocol_clamp_axis(form_get_i32(body, "throttle", 0)),
        .steering = sem_protocol_clamp_axis(form_get_i32(body, "steering", 0)),
        .enable = form_get_i32(body, "enable", 0) != 0,
        .sequence = (uint32_t)form_get_i32(body, "sequence", 0),
    };
    xQueueOverwrite(s_command_queue, &command);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static const char *state_to_string(sem_vehicle_state_t state)
{
    switch (state) {
    case SEM_VEHICLE_STATE_STOPPED:
        return "stopped";
    case SEM_VEHICLE_STATE_RUNNING:
        return "running";
    case SEM_VEHICLE_STATE_OBSTACLE_SLOWDOWN:
        return "slowdown";
    case SEM_VEHICLE_STATE_OBSTACLE_STOP:
        return "obstacle_stop";
    case SEM_VEHICLE_STATE_LINK_LOST:
        return "link_lost";
    default:
        return "unknown";
    }
}

static esp_err_t telemetry_get_handler(httpd_req_t *req)
{
    sem_vehicle_telemetry_t telemetry = sem_protocol_default_telemetry();
    xQueuePeek(s_telemetry_queue, &telemetry, 0);

    char response[192] = {0};
    snprintf(response, sizeof(response),
             "{\"applied_throttle\":%d,\"applied_steering\":%d,\"distance_cm\":%u,\"state\":\"%s\",\"last_command_sequence\":%lu}",
             telemetry.applied_throttle,
             telemetry.applied_steering,
             telemetry.distance_cm,
             state_to_string(telemetry.state),
             (unsigned long)telemetry.last_command_sequence);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "start HTTP server");

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
    };
    const httpd_uri_t control_uri = {
        .uri = "/api/control",
        .method = HTTP_POST,
        .handler = control_post_handler,
    };
    const httpd_uri_t telemetry_uri = {
        .uri = "/api/telemetry",
        .method = HTTP_GET,
        .handler = telemetry_get_handler,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &index_uri), TAG, "register /");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &control_uri), TAG,
                        "register /api/control");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &telemetry_uri), TAG,
                        "register /api/telemetry");

    ESP_LOGI(TAG, "web control ready: connect to WiFi %s and open http://192.168.4.1",
             SEM_WIFI_SSID);
    return ESP_OK;
}

static esp_err_t wifi_start_ap(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "init NVS");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "init netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "create event loop");
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_config), TAG, "init WiFi");

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = SEM_WIFI_SSID,
            .ssid_len = sizeof(SEM_WIFI_SSID) - 1,
            .channel = SEM_WIFI_CHANNEL,
            .max_connection = SEM_WIFI_MAX_CLIENTS,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set WiFi AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "set WiFi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start WiFi");

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s", SEM_WIFI_SSID);
    return ESP_OK;
}

static void control_task(void *arg)
{
    (void)arg;
    sem_control_command_t command = sem_protocol_default_command();
    sem_vehicle_telemetry_t telemetry = sem_protocol_default_telemetry();
    int64_t last_command_us = 0;

    while (true) {
        if (xQueueReceive(s_command_queue, &command, 0) == pdTRUE) {
            last_command_us = esp_timer_get_time();
        }

        const uint16_t distance_cm = read_distance_cm_stub();
        const bool link_ok = last_command_us > 0 &&
                             (esp_timer_get_time() - last_command_us) <
                                 (SEM_COMMAND_TIMEOUT_MS * 1000);
        int16_t throttle = (command.enable && link_ok) ? command.throttle : 0;
        int16_t steering = (command.enable && link_ok) ? command.steering : 0;

        throttle = sem_protocol_limit_throttle_by_distance(throttle, distance_cm);
        if (!link_ok) {
            steering = 0;
        }

        sem_vehicle_state_t state = sem_protocol_state_from_distance(distance_cm, link_ok);
        if (state == SEM_VEHICLE_STATE_RUNNING && throttle == 0 && steering == 0) {
            state = SEM_VEHICLE_STATE_STOPPED;
        }

        telemetry.applied_throttle = throttle;
        telemetry.applied_steering = steering;
        telemetry.distance_cm = distance_cm;
        telemetry.state = state;
        telemetry.last_command_sequence = command.sequence;

        xQueueOverwrite(s_motor_queue, &telemetry);
        xQueueOverwrite(s_telemetry_queue, &telemetry);

        vTaskDelay(pdMS_TO_TICKS(SEM_CONTROL_PERIOD_MS));
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

void app_main(void)
{
    s_command_queue = xQueueCreate(1, sizeof(sem_control_command_t));
    s_telemetry_queue = xQueueCreate(1, sizeof(sem_vehicle_telemetry_t));
    s_motor_queue = xQueueCreate(1, sizeof(sem_vehicle_telemetry_t));

    ESP_ERROR_CHECK(tb6612_init(&s_motor_driver));
    ESP_ERROR_CHECK(wifi_start_ap());
    ESP_ERROR_CHECK(start_web_server());

    xTaskCreate(control_task, "control", 4096, NULL, 6, NULL);
    xTaskCreate(motor_task, "motor", 4096, NULL, 5, NULL);
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, NULL);
}
