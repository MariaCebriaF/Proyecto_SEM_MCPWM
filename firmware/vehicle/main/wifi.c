#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h" 
#include "sem_protocol.h"

static const char *TAG = "SEM_WIFI"; 

// ----------- CREDENCIALES DE RED -----------
#define WIFI_SSID "pordefinir"
#define WIFI_PASS "pordefinir"
// -----------------------------------------

//Variables estática para guardar la cola y usarla cuando lleguen mensajes
static QueueHandle_t s_wifi_command_queue = NULL; 
static httpd_handle_t s_server = NULL;


static int32_t form_get_i32(const char *body, const char *key, int32_t fallback)
{
    char value[16] = {0};
    if (httpd_query_key_value(body, key, value, sizeof(value)) != ESP_OK) {
        return fallback;
    }
    return strtol(value, NULL, 10);
}



// ----------- MANEJADOR DE WEBSOCKET --------------------

static esp_err_t websocket_handler(httpd_req_t *req)
{
    // 1. Handshake inicial del WebSocket
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Nueva conexión WebSocket establecida.");
        return ESP_OK;
    }

    // 2. Recepción de tramas (frames) de datos
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Leemos primero para saber cuánto ocupa el mensaje
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    // Si tiene datos, reservamos memoria y leemos el contenido real
    if (ws_pkt.len > 0) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;
        
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        
        if (ret == ESP_OK) {
            char *body = (char *)ws_pkt.payload;
            
            sem_control_command_t command = {
                .version = SEM_PROTOCOL_VERSION,
                .throttle = sem_protocol_clamp_axis(form_get_i32(body, "throttle", 0)),
                .steering = sem_protocol_clamp_axis(form_get_i32(body, "steering", 0)),
                .enable = form_get_i32(body, "enable", 0) != 0,
                .sequence = (uint32_t)form_get_i32(body, "sequence", 0),
            };
            
            // Mandamos el comando a la Control Task sin bloquear
            xQueueOverwrite(s_wifi_command_queue, &command);
        }
        free(buf);
    }
    return ret;
}

//---------------FIN MANEJADOR DE WEBSOCKET --------------------

// ========================================================================================================================

// ----------------- INICIALIZACIÓN DE RED Y SERVIDOR -----------------

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Iniciando servidor en puerto %d", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret == ESP_OK) {
        // Configuramos la ruta /ws para que actúe como WebSocket
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

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Conectando al router...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Reintentando conexión...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "¡Conectado! IP asignada: " IPSTR, IP2STR(&event->ip_info.ip));
        
        // Cuando tenemos IP, levantamos el servidor para escuchar a tu web
        if (s_server == NULL) {
            start_web_server();
        }
    }
}

esp_err_t wifi_service_init(QueueHandle_t command_queue) {
    s_wifi_command_queue = command_queue;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Fallo al iniciar NVS");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Fallo al iniciar netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Fallo al crear event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Fallo al iniciar core Wi-Fi");

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Fallo al establecer modo STA");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Fallo al aplicar credenciales");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Fallo al arrancar Wi-Fi");

    return ESP_OK;
}


// ------------------ FIN INICIALIZACIÓN DE RED Y SERVIDOR -----------------



/* Código antiguo HTTP e implementación HTML
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

*/