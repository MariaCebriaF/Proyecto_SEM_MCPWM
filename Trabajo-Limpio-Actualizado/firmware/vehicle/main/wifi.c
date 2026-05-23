#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h" 
#include "sem_protocol.h"
#include <string.h>

// Variables Globales Externas (Definidas en app_main.c) 
extern volatile uint8_t g_modo;
extern portMUX_TYPE spinlock_modo;

extern volatile uint16_t g_distancia_cm; 
extern portMUX_TYPE spinlock_ultrasonidos; 

static const char *TAG = "SEM_WIFI"; 

// ----------- CREDENCIALES DE LA RED QUE VA A CREAR EL COCHE -----------
#define WIFI_SSID "SEM-PICAR"
#define WIFI_PASS "12345678" // ¡Ojo! Pon una de al menos 8 caracteres
// ----------------------------------------------------------------------

// Variables estáticas para guardar la cola y el servidor
static QueueHandle_t s_wifi_command_queue = NULL; 
static httpd_handle_t s_server = NULL;

// ==================== MANEJADOR DE WEBSOCKET ====================
static esp_err_t websocket_handler(httpd_req_t *req)
{
    // 1. Handshake inicial del WebSocket
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Nueva conexion WebSocket establecida.");
        return ESP_OK;
    }

    // 2. Recepción de tramas (frames) de datos
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Leemos primero para saber cuánto ocupa el mensaje
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;
    
    // Si tiene datos, reservamos memoria y leemos el contenido real
    if (ws_pkt.len > 0) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM; 
        
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        
        if (ret == ESP_OK) {
            char *body = (char *)ws_pkt.payload;
            
            /* 1. Extraer los datos enviados por React usando sscanf */
            int throttle_in = 0, steering_in = 0, enable_in = 0;
            unsigned long sequence_in = 0;
            int modo_in = 0;

            sscanf(body, "throttle=%d&steering=%d&enable=%d&sequence=%lu&modo=%d",
                   &throttle_in, &steering_in, &enable_in, &sequence_in, &modo_in);

            /* 2. Actualizar la variable global del Modo de forma segura */
            taskENTER_CRITICAL(&spinlock_modo);
            g_modo = (uint8_t)modo_in;
            taskEXIT_CRITICAL(&spinlock_modo);
            
            /* 3. Empaquetar los comandos de movimiento para la control_task */
            sem_control_command_t command = {
                .version = SEM_PROTOCOL_VERSION,
                .throttle = sem_protocol_clamp_axis(throttle_in),
                .steering = sem_protocol_clamp_axis(steering_in),
                .enable = enable_in != 0,
                .sequence = (uint32_t)sequence_in,
            };
            
            /* 4. Mandamos el comando a la Control Task sin bloquear */
            if (s_wifi_command_queue != NULL) {
                xQueueOverwrite(s_wifi_command_queue, &command);
            }
        }
        
        /* 5. Leer la distancia y responder a React con un JSON */
        uint16_t dist;
        taskENTER_CRITICAL(&spinlock_ultrasonidos);
        dist = g_distancia_cm;
        taskEXIT_CRITICAL(&spinlock_ultrasonidos);

        char json_reply[64];
        snprintf(json_reply, sizeof(json_reply), "{\"distance_cm\": %u}", dist);
        
        httpd_ws_frame_t ws_reply = {
            .payload = (uint8_t*)json_reply,
            .len = strlen(json_reply),
            .type = HTTPD_WS_TYPE_TEXT
        };
        httpd_ws_send_frame(req, &ws_reply);

        // Liberar la memoria RAM para evitar memory leaks
        free(buf); 
    }
    return ret;
}


// ==================== SERVIDOR HTTP ====================
static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Iniciando servidor en puerto %d", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret == ESP_OK) {
        httpd_uri_t ws_uri = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = websocket_handler,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(s_server, &ws_uri);
        ESP_LOGI(TAG, "Servidor WebSocket listo en la ruta /ws");
    }
    return ret;
}


// ==================== INICIALIZACION MODO AP (ROUTER) ====================
esp_err_t wifi_service_init(QueueHandle_t command_queue) {
    s_wifi_command_queue = command_queue;

    // 1. Iniciar memoria NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Configurar la red en Modo Access Point
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = 1,
        },
    };
    
    // Si la clave esta vacia, abrimos la red
    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "==== WIFI AP CREADO ====");
    ESP_LOGI(TAG, "SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "PASS: %s", WIFI_PASS);
    ESP_LOGI(TAG, "Conectate con el ordenador y abre React (La IP del robot es 192.168.4.1)");

    // 3. Encender el servidor
    return start_web_server();
}
