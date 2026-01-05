#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"

#define TAG "HOST_SERVER"

/* WiFi Configuration */
#define WIFI_SSID      "Restaurant_WiFi"
#define WIFI_PASS      "restaurant123"
#define WIFI_CHANNEL   1
#define MAX_CONNECTIONS 4

/* Order Storage */
typedef struct {
    int id;
    int table_number;
    char item_name[32];
    int price;
    uint32_t timestamp;
    bool is_active;
} order_t;

#define MAX_ORDERS 50
static order_t orders[MAX_ORDERS];
static int order_count = 0;
static int next_order_id = 1;

/* HTTP Server Handle */
static httpd_handle_t server = NULL;

/* ==================== ORDER MANAGEMENT ==================== */
static void add_order(int table, const char *item, int price)
{
    if (order_count >= MAX_ORDERS) {
        ESP_LOGW(TAG, "Order buffer full, removing oldest");
        // Shift orders
        for (int i = 0; i < MAX_ORDERS - 1; i++) {
            orders[i] = orders[i + 1];
        }
        order_count = MAX_ORDERS - 1;
    }
    
    orders[order_count].id = next_order_id++;
    orders[order_count].table_number = table;
    strncpy(orders[order_count].item_name, item, sizeof(orders[order_count].item_name) - 1);
    orders[order_count].price = price;
    orders[order_count].timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    orders[order_count].is_active = true;
    order_count++;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "NEW ORDER RECEIVED!");
    ESP_LOGI(TAG, "Order ID: %d", orders[order_count - 1].id);
    ESP_LOGI(TAG, "Table: %d", table);
    ESP_LOGI(TAG, "Item: %s", item);
    ESP_LOGI(TAG, "Price: Rs.%d", price);
    ESP_LOGI(TAG, "Total Orders: %d", order_count);
    ESP_LOGI(TAG, "========================================");
}

static bool complete_order(int order_id)
{
    for (int i = 0; i < order_count; i++) {
        if (orders[i].id == order_id && orders[i].is_active) {
            orders[i].is_active = false;
            ESP_LOGI(TAG, "Order #%d completed: %s", order_id, orders[i].item_name);
            return true;
        }
    }
    return false;
}

static int get_active_order_count(void)
{
    int count = 0;
    for (int i = 0; i < order_count; i++) {
        if (orders[i].is_active) {
            count++;
        }
    }
    return count;
}

/* ==================== WEB PAGE HTML ==================== */
static const char *get_webpage_html(void)
{
    static char html[8192];
    
    snprintf(html, sizeof(html),
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Restaurant Dashboard</title>"
        "<style>"
        "body { font-family: Arial; margin: 20px; background: #f0f0f0; }"
        "h1 { color: #333; text-align: center; background: #4CAF50; color: white; padding: 20px; border-radius: 10px; }"
        ".container { max-width: 1200px; margin: 0 auto; }"
        ".stats { display: flex; justify-content: center; margin: 20px 0; }"
        ".stat-box { background: white; padding: 30px 50px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); text-align: center; }"
        ".stat-box h2 { color: #4CAF50; margin: 0; font-size: 48px; }"
        ".stat-box p { color: #666; margin: 5px 0 0 0; font-size: 18px; }"
        ".orders { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); margin-top: 20px; }"
        ".order-item { border-left: 4px solid #4CAF50; padding: 15px; margin: 10px 0; background: #f9f9f9; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; }"
        ".order-item.completed { opacity: 0.5; border-left-color: #999; }"
        ".order-content { flex: 1; }"
        ".order-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }"
        ".table-badge { background: #4CAF50; color: white; padding: 5px 15px; border-radius: 20px; font-weight: bold; }"
        ".price { color: #4CAF50; font-size: 20px; font-weight: bold; }"
        ".timestamp { color: #999; font-size: 12px; }"
        ".no-orders { text-align: center; color: #999; padding: 40px; }"
        ".complete-btn { background: #4CAF50; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; font-size: 14px; font-weight: bold; }"
        ".complete-btn:hover { background: #45a049; }"
        ".complete-btn:disabled { background: #ccc; cursor: not-allowed; }"
        ".completed-text { color: #999; font-weight: bold; }"
        "</style>"
        "<script>"
        "function completeOrder(orderId) {"
        "  fetch('/complete?id=' + orderId, { method: 'POST' })"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      if (data.status === 'success') {"
        "        location.reload();"
        "      }"
        "    })"
        "    .catch(err => console.error('Error:', err));"
        "}"
        "setInterval(() => location.reload(), 10000);"
        "</script>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h1>🍽️ Smart Restaurant Dashboard</h1>"
        
        "<div class='stats'>"
        "<div class='stat-box'>"
        "<h2>%d</h2>"
        "<p>Active Orders</p>"
        "</div>"
        "</div>"
        
        "<div class='orders'>"
        "<h2>Recent Orders</h2>",
        get_active_order_count()
    );
    
    if (order_count == 0) {
        strcat(html, "<div class='no-orders'>No orders yet. Waiting for customers...</div>");
    } else {
        for (int i = order_count - 1; i >= 0; i--) {
            char order_html[512];
            
            if (orders[i].is_active) {
                snprintf(order_html, sizeof(order_html),
                    "<div class='order-item'>"
                    "<div class='order-content'>"
                    "<div class='order-header'>"
                    "<span class='table-badge'>Table %d</span>"
                    "<span class='price'>Rs.%d</span>"
                    "</div>"
                    "<div style='font-size: 18px; margin: 5px 0;'><strong>%s</strong></div>"
                    "<div class='timestamp'>%lu seconds ago | Order #%d</div>"
                    "</div>"
                    "<button class='complete-btn' onclick='completeOrder(%d)'>Complete</button>"
                    "</div>",
                    orders[i].table_number,
                    orders[i].price,
                    orders[i].item_name,
                    (xTaskGetTickCount() * portTICK_PERIOD_MS / 1000) - orders[i].timestamp,
                    orders[i].id,
                    orders[i].id
                );
            } else {
                snprintf(order_html, sizeof(order_html),
                    "<div class='order-item completed'>"
                    "<div class='order-content'>"
                    "<div class='order-header'>"
                    "<span class='table-badge' style='background: #999;'>Table %d</span>"
                    "<span class='price' style='color: #999;'>Rs.%d</span>"
                    "</div>"
                    "<div style='font-size: 18px; margin: 5px 0;'><strong>%s</strong></div>"
                    "<div class='timestamp'>Order #%d</div>"
                    "</div>"
                    "<span class='completed-text'>✓ Completed</span>"
                    "</div>",
                    orders[i].table_number,
                    orders[i].price,
                    orders[i].item_name,
                    orders[i].id
                );
            }
            
            strcat(html, order_html);
        }
    }
    
    strcat(html,
        "</div>"
        "<div style='text-align: center; margin-top: 20px; color: #999;'>"
        "<p>Auto-refresh every 10 seconds</p>"
        "</div>"
        "</div>"
        "</body>"
        "</html>"
    );
    
    return html;
}

/* ==================== HTTP HANDLERS ==================== */
static esp_err_t root_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Web page requested");
    
    const char *html = get_webpage_html();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    
    return ESP_OK;
}

static esp_err_t order_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    
    content[ret] = '\0';
    ESP_LOGI(TAG, "Received order data: %s", content);
    
    // Parse JSON
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        ESP_LOGE(TAG, "JSON parse error");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *table = cJSON_GetObjectItem(json, "table");
    cJSON *item = cJSON_GetObjectItem(json, "item");
    cJSON *price = cJSON_GetObjectItem(json, "price");
    
    if (cJSON_IsNumber(table) && cJSON_IsString(item) && cJSON_IsNumber(price)) {
        add_order(table->valueint, item->valuestring, price->valueint);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"Order received\"}");
    } else {
        ESP_LOGE(TAG, "Missing required fields");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t complete_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            int order_id = atoi(param);
            ESP_LOGI(TAG, "Completing order #%d", order_id);
            
            if (complete_order(order_id)) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"Order completed\"}");
            } else {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Order not found\"}");
            }
            
            return ESP_OK;
        }
    }
    
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
}

/* ==================== HTTP SERVER SETUP ==================== */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        httpd_uri_t order_uri = {
            .uri = "/order",
            .method = HTTP_POST,
            .handler = order_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &order_uri);
        
        httpd_uri_t complete_uri = {
            .uri = "/complete",
            .method = HTTP_POST,
            .handler = complete_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &complete_uri);
        
        ESP_LOGI(TAG, "HTTP server started successfully");
        return server;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

/* ==================== WIFI SETUP ==================== */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "Client connected, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "Client disconnected, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    }
}

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WiFi AP Started");
    ESP_LOGI(TAG, "SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "Password: %s", WIFI_PASS);
    ESP_LOGI(TAG, "Channel: %d", WIFI_CHANNEL);
    ESP_LOGI(TAG, "IP Address: 192.168.4.1");
    ESP_LOGI(TAG, "Web Dashboard: http://192.168.4.1");
    ESP_LOGI(TAG, "========================================");
}

/* ==================== MAIN ==================== */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Restaurant Host Server Starting");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize WiFi AP
    wifi_init_softap();
    
    // Start web server
    start_webserver();
    
    ESP_LOGI(TAG, "System ready - Waiting for orders...");
}
