#include "network.h"
#include "app_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>  // for malloc/free

static const char *TAG = "NETWORK";

static EventGroupHandle_t wifi_event_group;
static bool wifi_connected = false;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi connecting...");
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        
        // Auto-reconnect with delay
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
        
        if (retry_count < WIFI_MAX_RETRY) {
            retry_count++;
            ESP_LOGW(TAG, "Retry %d/%d", retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi connection failed");
        }
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        wifi_connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void network_wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // ✅ DISABLE POWER SAVING - Critical fix!
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi power save DISABLED");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi initialization complete");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", WIFI_SSID);
    }
}

bool network_wifi_is_connected(void) {
    return wifi_connected;
}

bool network_send_order(uint8_t table_id, cart_t *cart, bool append) {
    ESP_LOGI(TAG, "=== SEND ORDER ===");
    
    // ✅ Wait for connection if disconnected
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected, waiting...");
        for (int i = 0; i < 10; i++) {
            if (wifi_connected) break;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        
        if (!wifi_connected) {
            ESP_LOGE(TAG, "WiFi still disconnected!");
            return false;
        }
    }
    
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", HOST_IP, HOST_PORT, API_ORDER);
    
    // ✅ FIX: Allocate body buffer on HEAP instead of STACK
    char *body = (char *)malloc(2048);
    if (!body) {
        ESP_LOGE(TAG, "Failed to allocate memory for body");
        return false;
    }
    
    int offset = snprintf(body, 2048,
                         "{\"table_id\":%d,\"append\":%s,\"items\":[",
                         table_id, append ? "true" : "false");
    
    for (int i = 0; i < cart->count; i++) {
        offset += snprintf(body + offset, 2048 - offset,
                          "{\"id\":%d,\"name\":\"%s\",\"price\":%d,\"qty\":%d}%s",
                          cart->items[i].item_id,
                          cart->items[i].name,
                          cart->items[i].price,
                          cart->items[i].quantity,
                          (i < cart->count - 1) ? "," : "");
    }
    
    offset += snprintf(body + offset, 2048 - offset, "],\"total\":%lu}",
                      (unsigned long)cart->total);
    
    ESP_LOGI(TAG, "POST %s", url);
    ESP_LOGD(TAG, "Body: %s", body);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,  // 10 second timeout
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(body);  // ✅ Free allocated memory
        return false;
    }
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));
    
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    
    esp_http_client_cleanup(client);
    free(body);  // ✅ Free allocated memory
    
    if (err == ESP_OK && status == 200) {
        ESP_LOGI(TAG, "Order sent successfully (status=%d)", status);
        return true;
    } else {
        ESP_LOGE(TAG, "Order send failed: %s (status=%d)", esp_err_to_name(err), status);
        return false;
    }
}

bool network_get_table_status(uint8_t table_id, char *response_buf, size_t buf_size) {
    if (!wifi_connected) return false;
    
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s?table_id=%d",
            HOST_IP, HOST_PORT, API_TABLE_STATUS, table_id);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 3000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }
    
    int content_length = esp_http_client_fetch_headers(client);
    int total_read = 0;
    
    if (content_length > 0 && content_length < buf_size) {
        total_read = esp_http_client_read_response(client, response_buf, buf_size - 1);
    }
    
    if (total_read > 0) {
        response_buf[total_read] = '\0';
    }
    
    esp_http_client_cleanup(client);
    return (total_read > 0);
}


bool network_request_bill(uint8_t table_id) {
    if (!wifi_connected) return false;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", HOST_IP, HOST_PORT, API_REQUEST_BILL);

    char body[64];
    snprintf(body, sizeof(body), "{\"table_id\":%d}", table_id);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        ESP_LOGI(TAG, "Bill requested");
        return true;
    }

    ESP_LOGE(TAG, "Bill request failed");
    return false;
}

bool network_send_payment_method(uint8_t table_id, const char *method) {
    if (!wifi_connected) return false;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", HOST_IP, HOST_PORT, API_PAYMENT);

    char body[128];
    snprintf(body, sizeof(body), "{\"table_id\":%d,\"method\":\"%s\"}", table_id, method);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        ESP_LOGI(TAG, "Payment method sent: %s", method);
        return true;
    }

    ESP_LOGE(TAG, "Payment method send failed");
    return false;
}
