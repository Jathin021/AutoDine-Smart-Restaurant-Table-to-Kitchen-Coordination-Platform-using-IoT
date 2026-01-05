#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "cJSON.h"

#define TAG "CLIENT"

/* I2C Configuration */
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_SDA_IO         21
#define I2C_SCL_IO         22
#define I2C_FREQ_HZ        400000
#define SSD1306_ADDR       0x3C

/* Button Configuration */
#define BTN_SCROLL         18
#define BTN_SELECT         19
#define DEBOUNCE_DELAY_MS  300
#define DOUBLE_PRESS_WINDOW_MS  600

/* WiFi Configuration */
#define WIFI_SSID          "Restaurant_WiFi"
#define WIFI_PASS          "restaurant123"
#define SERVER_IP          "192.168.4.1"
#define HOST_URL           "http://192.168.4.1/order"
#define STATUS_URL         "http://192.168.4.1/status"
#define REQUEST_BILL_URL   "http://192.168.4.1/request_bill"
#define CHECK_BILL_URL     "http://192.168.4.1/check_bill_status"
#define BILL_URL           "http://192.168.4.1/get_bill"
#define PAYMENT_METHOD_URL "http://192.168.4.1/payment_method"
#define CHECK_PAYMENT_URL  "http://192.168.4.1/check_payment"

/* Table Number */
#define TABLE_NUMBER       1

/* Order Status Enum */
typedef enum {
    ORDER_PENDING = 0,
    ORDER_ACCEPTED = 1,
    ORDER_REJECTED = 2,
    ORDER_PREPARING = 3,
    ORDER_READY = 4,
    ORDER_COMPLETED = 5
} order_status_t;

/* Application States */
typedef enum {
    STATE_MENU = 0,
    STATE_WAITING_ACCEPTANCE,
    STATE_ACCEPTED,
    STATE_REJECTED,
    STATE_READY,
    STATE_BILL_REQUESTED,           // NEW: Waiting for chef to generate bill
    STATE_BILL_DISPLAY,             // NEW: Bill displayed, waiting for button press
    STATE_PAYMENT_CHOICE,           // NEW: Showing payment options
    STATE_PAYMENT_SELECTED,         // NEW: Payment method selected
    STATE_PAYMENT_DONE
} app_state_t;

/* Bill Structure */
typedef struct {
    int item_count;
    int subtotal;
    int gst;
    int total;
    bool fetched;
} bill_info_t;

/* Menu Items */
const char *menu_items[] = {
    "Tea      Rs.10",
    "Coffee   Rs.20",
    "Samosa   Rs.15",
    "Vada     Rs.25",
    "Dosa     Rs.40",
    "Idli     Rs.30",
    "Upma     Rs.35",
    "Poha     Rs.25",
    "Paratha  Rs.45",
    "Chai     Rs.12"
};

const int menu_prices[] = {10, 20, 15, 25, 40, 30, 35, 25, 45, 12};
#define MENU_COUNT 10

/* Global State */
static int selected_item = 0;
static bool wifi_connected = false;
static bool selected_flags[MENU_COUNT] = {false};
static int selected_count = 0;
static int last_displayed_item = -1;
static int last_selected_count = -1;
static bool force_redraw = false;
static int current_order_id = -1;
static int current_section_id = 1;
static app_state_t app_state = STATE_MENU;
static bill_info_t current_bill = {0};

/* 5x7 Font */
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40}, {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20}, {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18}, {0x08, 0x7E, 0x09, 0x01, 0x02}, {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00}, {0x20, 0x40, 0x44, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00}, {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38}, {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C}, {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C}, {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00}, {0x00, 0x00, 0x7F, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00}, {0x08, 0x04, 0x08, 0x10, 0x08}, {0x7F, 0x7F, 0x7F, 0x7F, 0x7F}
};

/* ==================== I2C & OLED ==================== */
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static void ssd1306_write_command(uint8_t cmd)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, 0x00, true);
    i2c_master_write_byte(handle, cmd, true);
    i2c_master_stop(handle);
    i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(handle);
}

static void ssd1306_init_display(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    ssd1306_write_command(0xAE);
    ssd1306_write_command(0xD5); ssd1306_write_command(0x80);
    ssd1306_write_command(0xA8); ssd1306_write_command(0x3F);
    ssd1306_write_command(0xD3); ssd1306_write_command(0x00);
    ssd1306_write_command(0x40);
    ssd1306_write_command(0x8D); ssd1306_write_command(0x14);
    ssd1306_write_command(0x20); ssd1306_write_command(0x00);
    ssd1306_write_command(0xA1);
    ssd1306_write_command(0xC8);
    ssd1306_write_command(0xDA); ssd1306_write_command(0x12);
    ssd1306_write_command(0x81); ssd1306_write_command(0xCF);
    ssd1306_write_command(0xD9); ssd1306_write_command(0xF1);
    ssd1306_write_command(0xDB); ssd1306_write_command(0x40);
    ssd1306_write_command(0xA4);
    ssd1306_write_command(0xA6);
    ssd1306_write_command(0xAF);
}

static void ssd1306_clear_screen(void)
{
    ssd1306_write_command(0x21); ssd1306_write_command(0); ssd1306_write_command(127);
    ssd1306_write_command(0x22); ssd1306_write_command(0); ssd1306_write_command(7);
    
    for (int page = 0; page < 8; page++) {
        i2c_cmd_handle_t handle = i2c_cmd_link_create();
        i2c_master_start(handle);
        i2c_master_write_byte(handle, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(handle, 0x40, true);
        for (int col = 0; col < 128; col++) {
            i2c_master_write_byte(handle, 0x00, true);
        }
        i2c_master_stop(handle);
        i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(handle);
    }
}

static void ssd1306_draw_text(uint8_t page, uint8_t col, const char *text)
{
    if (text == NULL || strlen(text) == 0) return;
    
    ssd1306_write_command(0x22); ssd1306_write_command(page); ssd1306_write_command(page);
    ssd1306_write_command(0x21); ssd1306_write_command(col); ssd1306_write_command(127);
    
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, 0x40, true);
    
    for (int i = 0; text[i] != '\0' && i < 20; i++) {
        uint8_t c = text[i];
        if (c >= 32 && c <= 126) {
            const uint8_t *glyph = font5x7[c - 32];
            for (int j = 0; j < 5; j++) {
                i2c_master_write_byte(handle, glyph[j], true);
            }
            i2c_master_write_byte(handle, 0x00, true);
        }
    }
    
    i2c_master_stop(handle);
    i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(handle);
}

/* ==================== WiFi ==================== */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi starting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "✓ WiFi Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ==================== HTTP Functions ==================== */
static int check_order_status(int order_id)
{
    if (!wifi_connected || order_id <= 0) return -1;
    
    char url[128];
    snprintf(url, sizeof(url), "%s?id=%d", STATUS_URL, order_id);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    int status = -1;
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        
        if (content_length > 0) {
            char *buffer = malloc(content_length + 1);
            if (buffer != NULL) {
                int read_len = esp_http_client_read_response(client, buffer, content_length);
                if (read_len > 0) {
                    buffer[read_len] = '\0';
                    cJSON *json = cJSON_Parse(buffer);
                    if (json != NULL) {
                        cJSON *status_obj = cJSON_GetObjectItem(json, "order_status");
                        if (cJSON_IsNumber(status_obj)) {
                            status = status_obj->valueint;
                        }
                        cJSON_Delete(json);
                    }
                }
                free(buffer);
            }
        }
        esp_http_client_close(client);
    }
    
    esp_http_client_cleanup(client);
    return status;
}

// NEW: Request bill from server
static bool request_bill_from_server(int section_id)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }
    
    char url[128];
    snprintf(url, sizeof(url), "%s?section=%d", REQUEST_BILL_URL, section_id);
    
    ESP_LOGI(TAG, "Requesting bill for section %d", section_id);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .method = HTTP_METHOD_POST,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool success = false;
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        success = (status_code == 200);
        ESP_LOGI(TAG, "Bill request status: %d", status_code);
    }
    
    esp_http_client_cleanup(client);
    return success;
}

// NEW: Check if bill has been generated by chef
static bool check_bill_generated(int section_id)
{
    if (!wifi_connected) return false;
    
    char url[128];
    snprintf(url, sizeof(url), "%s?section=%d", CHECK_BILL_URL, section_id);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool generated = false;
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        
        if (content_length > 0) {
            char *buffer = malloc(content_length + 1);
            if (buffer != NULL) {
                int read_len = esp_http_client_read_response(client, buffer, content_length);
                if (read_len > 0) {
                    buffer[read_len] = '\0';
                    cJSON *json = cJSON_Parse(buffer);
                    if (json != NULL) {
                        cJSON *gen_obj = cJSON_GetObjectItem(json, "bill_generated");
                        if (cJSON_IsBool(gen_obj)) {
                            generated = cJSON_IsTrue(gen_obj);
                        }
                        cJSON_Delete(json);
                    }
                }
                free(buffer);
            }
        }
        esp_http_client_close(client);
    }
    
    esp_http_client_cleanup(client);
    return generated;
}

static bool fetch_bill(int section_id)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }
    
    char url[128];
    snprintf(url, sizeof(url), "%s?section=%d", BILL_URL, section_id);
    
    ESP_LOGI(TAG, "Fetching bill from: %s", url);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool success = false;
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "HTTP Status: %d, Content Length: %d", status_code, content_length);
        
        if (status_code == 200 && content_length > 0) {
            char *buffer = malloc(content_length + 1);
            if (buffer != NULL) {
                int read_len = esp_http_client_read_response(client, buffer, content_length);
                if (read_len > 0) {
                    buffer[read_len] = '\0';
                    ESP_LOGI(TAG, "Bill data: %s", buffer);
                    
                    cJSON *json = cJSON_Parse(buffer);
                    if (json != NULL) {
                        cJSON *subtotal = cJSON_GetObjectItem(json, "subtotal");
                        cJSON *gst = cJSON_GetObjectItem(json, "gst");
                        cJSON *total = cJSON_GetObjectItem(json, "total");
                        cJSON *item_count = cJSON_GetObjectItem(json, "item_count");
                        
                        if (cJSON_IsNumber(subtotal) && cJSON_IsNumber(gst) && 
                            cJSON_IsNumber(total) && cJSON_IsNumber(item_count)) {
                            
                            current_bill.subtotal = subtotal->valueint;
                            current_bill.gst = gst->valueint;
                            current_bill.total = total->valueint;
                            current_bill.item_count = item_count->valueint;
                            current_bill.fetched = true;
                            
                            success = true;
                            ESP_LOGI(TAG, "✓ Bill: Items=%d, Total=Rs.%d", 
                                    current_bill.item_count, current_bill.total);
                        }
                        cJSON_Delete(json);
                    }
                }
                free(buffer);
            }
        }
        
        esp_http_client_close(client);
    }
    
    esp_http_client_cleanup(client);
    return success;
}

static bool send_payment_method(int section_id, const char *method)
{
    if (!wifi_connected) return false;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "section", section_id);
    cJSON_AddStringToObject(json, "method", method);
    
    char *json_str = cJSON_PrintUnformatted(json);
    ESP_LOGI(TAG, "Sending payment: %s", json_str);
    
    esp_http_client_config_t config = {
        .url = PAYMENT_METHOD_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));
    
    esp_err_t err = esp_http_client_perform(client);
    bool success = (err == ESP_OK && esp_http_client_get_status_code(client) == 200);
    
    esp_http_client_cleanup(client);
    cJSON_Delete(json);
    free(json_str);
    
    ESP_LOGI(TAG, "Payment method %s: %s", success ? "sent" : "failed", method);
    return success;
}

static bool check_payment_verified(int section_id)
{
    if (!wifi_connected) return false;
    
    char url[128];
    snprintf(url, sizeof(url), "%s?section=%d", CHECK_PAYMENT_URL, section_id);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool verified = false;
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        
        if (content_length > 0) {
            char *buffer = malloc(content_length + 1);
            if (buffer != NULL) {
                int read_len = esp_http_client_read_response(client, buffer, content_length);
                if (read_len > 0) {
                    buffer[read_len] = '\0';
                    cJSON *json = cJSON_Parse(buffer);
                    if (json != NULL) {
                        cJSON *verified_obj = cJSON_GetObjectItem(json, "verified");
                        if (cJSON_IsBool(verified_obj)) {
                            verified = cJSON_IsTrue(verified_obj);
                        }
                        cJSON_Delete(json);
                    }
                }
                free(buffer);
            }
        }
        esp_http_client_close(client);
    }
    
    esp_http_client_cleanup(client);
    return verified;
}

static int send_orders_to_host(void)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return -1;
    }
    
    if (selected_count == 0) {
        ESP_LOGW(TAG, "No items selected");
        return -1;
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "table", TABLE_NUMBER);
    cJSON_AddNumberToObject(root, "section", current_section_id);
    
    cJSON *items = cJSON_CreateArray();
    cJSON *prices = cJSON_CreateArray();
    
    for (int i = 0; i < MENU_COUNT; i++) {
        if (selected_flags[i]) {
            cJSON_AddItemToArray(items, cJSON_CreateString(menu_items[i]));
            cJSON_AddItemToArray(prices, cJSON_CreateNumber(menu_prices[i]));
        }
    }
    
    cJSON_AddItemToObject(root, "items", items);
    cJSON_AddItemToObject(root, "prices", prices);
    
    char *json_str = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Sending order: %s", json_str);
    ESP_LOGI(TAG, "========================================");
    
    esp_http_client_config_t config = {
        .url = HOST_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));
    
    int order_id = -1;
    
    esp_err_t err = esp_http_client_open(client, strlen(json_str));
    
    if (err == ESP_OK) {
        int wlen = esp_http_client_write(client, json_str, strlen(json_str));
        ESP_LOGI(TAG, "Written: %d bytes", wlen);
        
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "HTTP Status: %d, Content Length: %d", status_code, content_length);
        
        if (status_code == 200 && content_length > 0) {
            char *response = malloc(content_length + 1);
            if (response != NULL) {
                memset(response, 0, content_length + 1);
                
                int read_len = esp_http_client_read_response(client, response, content_length);
                
                if (read_len > 0) {
                    response[read_len] = '\0';
                    ESP_LOGI(TAG, "Response (%d bytes): %s", read_len, response);
                    
                    cJSON *resp_json = cJSON_Parse(response);
                    if (resp_json != NULL) {
                        cJSON *status_obj = cJSON_GetObjectItem(resp_json, "status");
                        cJSON *id = cJSON_GetObjectItem(resp_json, "order_id");
                        
                        if (cJSON_IsString(status_obj) && strcmp(status_obj->valuestring, "success") == 0) {
                            if (cJSON_IsNumber(id)) {
                                order_id = id->valueint;
                                ESP_LOGI(TAG, "========================================");
                                ESP_LOGI(TAG, "✓ SUCCESS! Order ID: %d", order_id);
                                ESP_LOGI(TAG, "========================================");
                            }
                        }
                        cJSON_Delete(resp_json);
                    }
                }
                
                free(response);
            }
        }
        
        esp_http_client_close(client);
    }
    
    esp_http_client_cleanup(client);
    cJSON_Delete(root);
    free(json_str);
    
    if (order_id > 0) {
        for (int i = 0; i < MENU_COUNT; i++) {
            selected_flags[i] = false;
        }
        selected_count = 0;
    }
    
    return order_id;
}

/* ==================== Display Functions (IMPROVED UI) ==================== */
static void display_menu(void)
{
    if (app_state != STATE_MENU) return;
    
    if (last_displayed_item == selected_item && 
        last_selected_count == selected_count && 
        !force_redraw) {
        return;
    }
    
    ssd1306_clear_screen();
    
    char header[32];
    snprintf(header, sizeof(header), "=== MENU [%d] ===", selected_count);
    ssd1306_draw_text(0, 0, header);
    
    int start = (selected_item / 5) * 5;
    for (int i = 0; i < 5 && (start + i) < MENU_COUNT; i++) {
        int idx = start + i;
        char line[24];
        char marker = (idx == selected_item) ? '>' : ' ';
        char bracket = selected_flags[idx] ? '*' : ' ';
        
        snprintf(line, sizeof(line), "%c%d.%c%s", marker, idx + 1, bracket, menu_items[idx]);
        ssd1306_draw_text(i + 2, 0, line);
    }
    
    ssd1306_draw_text(7, 0, "1x:Add 2x:Send");
    
    last_displayed_item = selected_item;
    last_selected_count = selected_count;
    force_redraw = false;
}

static void display_item_toggled(bool added)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 10, "============");
    ssd1306_draw_text(2, added ? 15 : 10, added ? "ADDED!" : "REMOVED!");
    ssd1306_draw_text(3, 10, "============");
    ssd1306_draw_text(5, 0, menu_items[selected_item]);
    
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "Cart: %d items", selected_count);
    ssd1306_draw_text(7, 5, cnt);
}

static void display_sending(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 10, "============");
    ssd1306_draw_text(2, 10, "SENDING...");
    ssd1306_draw_text(3, 10, "============");
    
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d items", selected_count);
    ssd1306_draw_text(5, 15, cnt);
    ssd1306_draw_text(7, 5, "Please wait...");
}

static void display_waiting(int order_id)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 5, "Order Sent!");
    ssd1306_draw_text(2, 5, "============");
    
    char id[32];
    snprintf(id, sizeof(id), "ID: #%d", order_id);
    ssd1306_draw_text(4, 20, id);
    ssd1306_draw_text(6, 5, "Waiting for");
    ssd1306_draw_text(7, 10, "chef...");
}

static void display_rejected(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 10, "============");
    ssd1306_draw_text(2, 20, "SORRY!");
    ssd1306_draw_text(3, 10, "============");
    ssd1306_draw_text(5, 10, "Order");
    ssd1306_draw_text(6, 5, "Cancelled");
    ssd1306_draw_text(7, 0, "Press Btn-1");
}

// UPDATED: Removed "2x Btn:Bill" text during preparing
static void display_accepted(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 5, "============");
    ssd1306_draw_text(2, 10, "ACCEPTED!");
    ssd1306_draw_text(3, 5, "============");
    ssd1306_draw_text(5, 5, "Preparing");
    ssd1306_draw_text(6, 5, "your food...");
    ssd1306_draw_text(7, 5, "Time:10-15min");
}

// UPDATED: Shows both "Order More" and "Get Bill" options
static void display_ready(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 5, "============");
    ssd1306_draw_text(2, 15, "READY!");
    ssd1306_draw_text(3, 5, "============");
    ssd1306_draw_text(5, 5, "Food is ready");
    ssd1306_draw_text(6, 5, "to serve!");
    ssd1306_draw_text(7, 0, "1x:Add 2x:Bill");
}

static void display_bill_requested(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 5, "Bill Request");
    ssd1306_draw_text(2, 10, "Sent!");
    ssd1306_draw_text(3, 5, "============");
    ssd1306_draw_text(5, 5, "Waiting for");
    ssd1306_draw_text(6, 5, "chef to");
    ssd1306_draw_text(7, 5, "generate...");
}

static void display_bill(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(0, 20, "=== BILL ===");
    
    char items_text[22];
    snprintf(items_text, sizeof(items_text), "Items: %d", current_bill.item_count);
    ssd1306_draw_text(2, 5, items_text);
    
    char subtotal[22];
    snprintf(subtotal, sizeof(subtotal), "Subtotal:%d", current_bill.subtotal);
    ssd1306_draw_text(3, 0, subtotal);
    
    char gst[22];
    snprintf(gst, sizeof(gst), "GST(18%%): %d", current_bill.gst);
    ssd1306_draw_text(4, 0, gst);
    
    ssd1306_draw_text(5, 0, "================");
    
    char total[22];
    snprintf(total, sizeof(total), "TOTAL: Rs.%d", current_bill.total);
    ssd1306_draw_text(6, 5, total);
    
    ssd1306_draw_text(7, 5, "Press any btn");
}

static void display_payment_options(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(0, 5, "============");
    ssd1306_draw_text(1, 5, "Select Payment");
    ssd1306_draw_text(2, 10, "Method:");
    ssd1306_draw_text(3, 5, "============");
    ssd1306_draw_text(5, 5, "Btn-1: CASH");
    ssd1306_draw_text(7, 5, "Btn-2: ONLINE");
}

static void display_payment_selected(const char *method)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 5, "============");
    ssd1306_draw_text(2, 10, "Payment:");
    ssd1306_draw_text(3, 15, method);
    ssd1306_draw_text(4, 5, "============");
    
    char total[24];
    snprintf(total, sizeof(total), "Amount:%d", current_bill.total);
    ssd1306_draw_text(6, 10, total);
    
    ssd1306_draw_text(7, 0, "Wait for chef");
}

static void display_payment_done(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(1, 5, "============");
    ssd1306_draw_text(2, 5, "Payment Done!");
    ssd1306_draw_text(3, 5, "============");
    ssd1306_draw_text(5, 5, "Visit Again!");
    ssd1306_draw_text(7, 10, "Thank You");
}

/* ==================== Status Check Task ==================== */
static void status_check_task(void *param)
{
    while (1) {
        // Order status monitoring
        if ((app_state == STATE_WAITING_ACCEPTANCE || app_state == STATE_ACCEPTED) && 
            current_order_id > 0) {
            
            int status = check_order_status(current_order_id);
            
            if (status >= 0) {
                if (status == ORDER_REJECTED && app_state != STATE_REJECTED) {
                    ESP_LOGI(TAG, "Order rejected");
                    app_state = STATE_REJECTED;
                    display_rejected();
                    
                } else if ((status == ORDER_ACCEPTED || status == ORDER_PREPARING) && 
                          app_state != STATE_ACCEPTED) {
                    ESP_LOGI(TAG, "Order accepted");
                    app_state = STATE_ACCEPTED;
                    display_accepted();
                    
                } else if (status == ORDER_READY && app_state != STATE_READY) {
                    ESP_LOGI(TAG, "Order ready");
                    app_state = STATE_READY;
                    display_ready();
                }
            }
        }
        
        // NEW: Check if bill has been generated by chef
        if (app_state == STATE_BILL_REQUESTED) {
            if (check_bill_generated(current_section_id)) {
                ESP_LOGI(TAG, "Bill has been generated by chef!");
                
                if (fetch_bill(current_section_id)) {
                    app_state = STATE_BILL_DISPLAY;
                    display_bill();
                } else {
                    ESP_LOGE(TAG, "Failed to fetch bill");
                    ssd1306_clear_screen();
                    ssd1306_draw_text(2, 10, "Bill Error!");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    app_state = STATE_READY;
                    display_ready();
                }
            }
        }
        
        // Payment verification monitoring
        if (app_state == STATE_PAYMENT_SELECTED) {
            if (check_payment_verified(current_section_id)) {
                ESP_LOGI(TAG, "========================================");
                ESP_LOGI(TAG, "✓ Payment verified!");
                ESP_LOGI(TAG, "========================================");
                
                app_state = STATE_PAYMENT_DONE;
                display_payment_done();
                vTaskDelay(pdMS_TO_TICKS(5000));
                
                // Reset for new session
                current_section_id++;
                current_order_id = -1;
                current_bill.fetched = false;
                memset(&current_bill, 0, sizeof(current_bill));
                app_state = STATE_MENU;
                force_redraw = true;
                
                ESP_LOGI(TAG, "========================================");
                ESP_LOGI(TAG, "New session: Section %d", current_section_id);
                ESP_LOGI(TAG, "========================================");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ==================== Main ==================== */
void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   RESTAURANT CLIENT STARTING");
    ESP_LOGI(TAG, "========================================");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_ERROR_CHECK(i2c_master_init());
    ssd1306_init_display();
    ssd1306_clear_screen();
    ssd1306_draw_text(3, 5, "Connecting...");
    
    gpio_reset_pin(BTN_SCROLL);
    gpio_reset_pin(BTN_SELECT);
    gpio_set_direction(BTN_SCROLL, GPIO_MODE_INPUT);
    gpio_set_direction(BTN_SELECT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_SCROLL, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BTN_SELECT, GPIO_PULLUP_ONLY);
    
    wifi_init_sta();
    
    int wait_count = 0;
    while (!wifi_connected && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(200));
        wait_count++;
    }
    
    if (wifi_connected) {
        ssd1306_clear_screen();
        ssd1306_draw_text(3, 10, "CONNECTED!");
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        ESP_LOGI(TAG, "✓ WiFi connected");
    } else {
        ESP_LOGE(TAG, "✗ WiFi connection failed");
        ssd1306_clear_screen();
        ssd1306_draw_text(3, 5, "WiFi Failed!");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    app_state = STATE_MENU;
    force_redraw = true;
    
    xTaskCreate(status_check_task, "status", 5120, NULL, 5, NULL);
    
    TickType_t last_scroll = 0, last_select = 0, first_select = 0;
    int select_count = 0;
    bool last_scroll_state = 1, last_select_state = 1;
    
    TickType_t first_scroll = 0;
    int scroll_count = 0;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   ✓ SYSTEM READY");
    ESP_LOGI(TAG, "========================================");
    
    while (1) {
        TickType_t now = xTaskGetTickCount();
        int scroll = gpio_get_level(BTN_SCROLL);
        int select = gpio_get_level(BTN_SELECT);
        
        // Return to menu from rejected
        if (app_state == STATE_REJECTED) {
            if (last_scroll_state == 0 && scroll == 1) {
                if ((now - last_scroll) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                    app_state = STATE_MENU;
                    force_redraw = true;
                    last_scroll = now;
                }
            }
        }
        
        // NEW: Double press to request bill (from ACCEPTED or READY)
               // UPDATED: Single press Button-1 when READY = Back to Menu (Order More)
        // Double press Button-1 when READY = Request Bill
        if (app_state == STATE_READY) {
            if (last_scroll_state == 0 && scroll == 1) {
                if ((now - last_scroll) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                    if (scroll_count == 0) {
                        first_scroll = now;
                        scroll_count = 1;
                    } else if (scroll_count == 1) {
                        uint32_t diff = (now - first_scroll) * portTICK_PERIOD_MS;
                        
                        if (diff < DOUBLE_PRESS_WINDOW_MS) {
                            // Double press - REQUEST BILL
                            ESP_LOGI(TAG, "========================================");
                            ESP_LOGI(TAG, "Requesting bill for section %d...", current_section_id);
                            ESP_LOGI(TAG, "========================================");
                            
                            if (request_bill_from_server(current_section_id)) {
                                app_state = STATE_BILL_REQUESTED;
                                display_bill_requested();
                            } else {
                                ESP_LOGE(TAG, "Bill request failed");
                                ssd1306_clear_screen();
                                ssd1306_draw_text(2, 10, "Request Failed!");
                                vTaskDelay(pdMS_TO_TICKS(2000));
                                display_ready();
                            }
                            
                            scroll_count = 0;
                        } else {
                            first_scroll = now;
                            scroll_count = 1;
                        }
                    }
                    last_scroll = now;
                }
            }
            
            // NEW: Single press timeout on Button-1 = Back to Menu (Order More)
            if (scroll_count == 1 && (now - first_scroll) * portTICK_PERIOD_MS >= DOUBLE_PRESS_WINDOW_MS) {
                ESP_LOGI(TAG, "Customer wants to order more items");
                app_state = STATE_MENU;
                force_redraw = true;
                scroll_count = 0;
            }
            
            // Also allow Button-2 double press for bill request
            if (last_select_state == 0 && select == 1) {
                if ((now - last_select) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                    if (select_count == 0) {
                        first_select = now;
                        select_count = 1;
                    } else if (select_count == 1) {
                        uint32_t diff = (now - first_select) * portTICK_PERIOD_MS;
                        
                        if (diff < DOUBLE_PRESS_WINDOW_MS) {
                            ESP_LOGI(TAG, "Requesting bill (Button-2)...");
                            
                            if (request_bill_from_server(current_section_id)) {
                                app_state = STATE_BILL_REQUESTED;
                                display_bill_requested();
                            }
                            
                            select_count = 0;
                        } else {
                            first_select = now;
                            select_count = 1;
                        }
                    }
                    last_select = now;
                }
            }
        }
        
        // NEW: Any button press on bill display shows payment options
        if (app_state == STATE_BILL_DISPLAY) {
            if ((last_scroll_state == 0 && scroll == 1) || (last_select_state == 0 && select == 1)) {
                if ((now - last_scroll) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS || 
                    (now - last_select) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                    
                    ESP_LOGI(TAG, "Showing payment options");
                    app_state = STATE_PAYMENT_CHOICE;
                    display_payment_options();
                    
                    last_scroll = now;
                    last_select = now;
                    scroll_count = 0;
                    select_count = 0;
                }
            }
        }
        
        // NEW: Payment method selection
        if (app_state == STATE_PAYMENT_CHOICE) {
            if (last_scroll_state == 0 && scroll == 1) {
                if ((now - last_scroll) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                    send_payment_method(current_section_id, "Cash");
                    app_state = STATE_PAYMENT_SELECTED;
                    display_payment_selected("CASH");
                    last_scroll = now;
                    scroll_count = 0;
                }
            }
            
            if (last_select_state == 0 && select == 1) {
                if ((now - last_select) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                    send_payment_method(current_section_id, "Online");
                    app_state = STATE_PAYMENT_SELECTED;
                    display_payment_selected("ONLINE");
                    last_select = now;
                    select_count = 0;
                }
            }
        }
        
        // Menu operations
        if (app_state == STATE_MENU) {
            if (last_scroll_state == 0 && scroll == 1) {
                if ((now - last_scroll) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                    selected_item = (selected_item + 1) % MENU_COUNT;
                    last_scroll = now;
                    scroll_count = 0;
                }
            }
            
            if (last_select_state == 0 && select == 1) {
                if ((now - last_select) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                    if (select_count == 0) {
                        first_select = now;
                        select_count = 1;
                        
                    } else if (select_count == 1) {
                        uint32_t diff = (now - first_select) * portTICK_PERIOD_MS;
                        
                        if (diff < DOUBLE_PRESS_WINDOW_MS) {
                            // Double press - send order
                            ESP_LOGI(TAG, "Sending order with %d items...", selected_count);
                            
                            display_sending();
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            
                            current_order_id = send_orders_to_host();
                            
                            if (current_order_id > 0) {
                                app_state = STATE_WAITING_ACCEPTANCE;
                                display_waiting(current_order_id);
                            } else {
                                ESP_LOGE(TAG, "Order send failed!");
                                ssd1306_clear_screen();
                                ssd1306_draw_text(2, 15, "FAILED!");
                                ssd1306_draw_text(4, 10, "Try again");
                                vTaskDelay(pdMS_TO_TICKS(2000));
                                force_redraw = true;
                            }
                            
                            select_count = 0;
                        } else {
                            first_select = now;
                            select_count = 1;
                        }
                    }
                    
                    last_select = now;
                }
            }
            
            // Single press timeout - toggle item
            if (select_count == 1 && (now - first_select) * portTICK_PERIOD_MS >= DOUBLE_PRESS_WINDOW_MS) {
                selected_flags[selected_item] = !selected_flags[selected_item];
                selected_count += selected_flags[selected_item] ? 1 : -1;
                
                display_item_toggled(selected_flags[selected_item]);
                vTaskDelay(pdMS_TO_TICKS(1500));
                
                force_redraw = true;
                select_count = 0;
            }
        }
        
        last_scroll_state = scroll;
        last_select_state = select;
        
        display_menu();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
