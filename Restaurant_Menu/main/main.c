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

#define TAG "RESTAURANT_CLIENT"

/* ---------- I2C CONFIG ---------- */
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_SDA_IO         21
#define I2C_SCL_IO         22
#define I2C_FREQ_HZ        400000
#define SSD1306_ADDR       0x3C

/* ---------- BUTTON CONFIG ---------- */
#define BTN_SCROLL         18
#define BTN_SELECT         19
#define DEBOUNCE_DELAY_MS  300
#define DOUBLE_PRESS_WINDOW_MS  600

/* ---------- WIFI CONFIG ---------- */
#define WIFI_SSID          "Restaurant_WiFi"
#define WIFI_PASS          "restaurant123"
#define HOST_URL           "http://192.168.4.1/order"

/* ---------- MENU DATA ---------- */
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

static int selected_item = 0;
static bool wifi_connected = false;

// Multi-select tracking
static bool selected_flags[MENU_COUNT] = {false};
static int selected_count = 0;

// Display state tracking (anti-flicker)
static int last_displayed_item = -1;
static int last_selected_count = -1;
static bool force_redraw = false;

/* ---------- SIMPLE 5x7 FONT ---------- */
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

/* ---------- I2C & OLED FUNCTIONS ---------- */
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
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t ssd1306_write_command(uint8_t command)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);
    i2c_master_write_byte(cmd, command, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t ssd1306_init_display(void)
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
    return ESP_OK;
}

static void ssd1306_clear_screen(void)
{
    ssd1306_write_command(0x21); ssd1306_write_command(0); ssd1306_write_command(127);
    ssd1306_write_command(0x22); ssd1306_write_command(0); ssd1306_write_command(7);
    for (int page = 0; page < 8; page++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, 0x40, true);
        for (int col = 0; col < 128; col++) {
            i2c_master_write_byte(cmd, 0x00, true);
        }
        i2c_master_stop(cmd);
        i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
    }
}

static void ssd1306_draw_text(uint8_t page, uint8_t col, const char *text)
{
    size_t len = strlen(text);
    if (len == 0) return;
    
    ssd1306_write_command(0x22); ssd1306_write_command(page); ssd1306_write_command(page);
    ssd1306_write_command(0x21); ssd1306_write_command(col); ssd1306_write_command(127);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x40, true);
    
    for (size_t i = 0; i < len && i < 20; i++) {
        uint8_t c = text[i];
        if (c >= 32 && c <= 126) {
            const uint8_t *glyph = font5x7[c - 32];
            for (int j = 0; j < 5; j++) {
                i2c_master_write_byte(cmd, glyph[j], true);
            }
            i2c_master_write_byte(cmd, 0x00, true);
        }
    }
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
}

/* ---------- BUTTON INIT ---------- */
static void buttons_init(void)
{
    gpio_reset_pin(BTN_SCROLL);
    gpio_reset_pin(BTN_SELECT);
    gpio_set_direction(BTN_SCROLL, GPIO_MODE_INPUT);
    gpio_set_direction(BTN_SELECT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_SCROLL, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BTN_SELECT, GPIO_PULLUP_ONLY);
}

/* ---------- WIFI ---------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(TAG, "WiFi Connected!");
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
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ---------- HTTP POST ---------- */
static void send_orders_to_host(void)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        ssd1306_clear_screen();
        ssd1306_draw_text(3, 10, "WiFi Error!");
        vTaskDelay(pdMS_TO_TICKS(2000));
        force_redraw = true;
        return;
    }
    
    if (selected_count == 0) {
        ESP_LOGW(TAG, "No items selected");
        ssd1306_clear_screen();
        ssd1306_draw_text(3, 5, "No Items!");
        vTaskDelay(pdMS_TO_TICKS(2000));
        force_redraw = true;
        return;
    }
    
    ESP_LOGI(TAG, "Sending %d orders...", selected_count);
    
    int success_count = 0;
    for (int i = 0; i < MENU_COUNT; i++) {
        if (selected_flags[i]) {
            char json_string[128];
            snprintf(json_string, sizeof(json_string),
                     "{\"table\":1,\"item\":\"%s\",\"price\":%d}",
                     menu_items[i], menu_prices[i]);
            
            esp_http_client_config_t config = {
                .url = HOST_URL,
                .method = HTTP_METHOD_POST,
                .timeout_ms = 5000,
            };
            
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, json_string, strlen(json_string));
            
            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK) {
                success_count++;
                ESP_LOGI(TAG, "Sent: %s", menu_items[i]);
            }
            
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    
    // Clear selections
    for (int i = 0; i < MENU_COUNT; i++) {
        selected_flags[i] = false;
    }
    selected_count = 0;
    
    // Show success
    ssd1306_clear_screen();
    ssd1306_draw_text(2, 5, "Orders Sent!");
    char msg[32];
    snprintf(msg, sizeof(msg), "%d items", success_count);
    ssd1306_draw_text(4, 15, msg);
    ssd1306_draw_text(6, 10, "Thank you!");
    ESP_LOGI(TAG, "All orders sent successfully!");
    
    force_redraw = true;
}

/* ---------- DISPLAY ---------- */
static void display_menu(void)
{
    // Only redraw if something changed (ANTI-FLICKER)
    if (last_displayed_item == selected_item && 
        last_selected_count == selected_count && 
        !force_redraw) {
        return;
    }
    
    ssd1306_clear_screen();
    
    // Header with selection count
    char header[32];
    snprintf(header, sizeof(header), "MENU [%d selected]", selected_count);
    ssd1306_draw_text(0, 0, header);
    
    // Show 5 items at a time
    int start_idx = (selected_item / 5) * 5;
    for (int i = 0; i < 5 && (start_idx + i) < MENU_COUNT; i++) {
        int idx = start_idx + i;
        char line[24];
        
        // Build display line
        char marker = (idx == selected_item) ? '>' : ' ';
        char bracket = selected_flags[idx] ? '*' : ' ';
        
        // Format: ">1.*Tea Rs.10" or " 2. Coffee Rs.20"
        if (idx < 9) {
            snprintf(line, sizeof(line), "%c%d.%c%s", marker, idx + 1, bracket, menu_items[idx]);
        } else {
            snprintf(line, sizeof(line), "%c%d%c%s", marker, idx + 1, bracket, menu_items[idx]);
        }
        
        ssd1306_draw_text(i + 2, 0, line);
    }
    
    // Footer instruction
    ssd1306_draw_text(7, 0, "1x=Add 2x=Send");
    
    last_displayed_item = selected_item;
    last_selected_count = selected_count;
    force_redraw = false;
}

static void display_item_toggled(bool added)
{
    ssd1306_clear_screen();
    
    if (added) {
        ssd1306_draw_text(2, 20, "ADDED!");
    } else {
        ssd1306_draw_text(2, 15, "REMOVED!");
    }
    
    // Show item name
    char item_text[24];
    int item_num = selected_item + 1;
    snprintf(item_text, sizeof(item_text), "%d. %s", item_num, menu_items[selected_item]);
    ssd1306_draw_text(4, 0, item_text);
    
    // Show total count
    char count[24];
    snprintf(count, sizeof(count), "Total: %d items", selected_count);
    ssd1306_draw_text(6, 5, count);
    
    force_redraw = true;
}

static void display_sending(void)
{
    ssd1306_clear_screen();
    ssd1306_draw_text(2, 15, "SENDING...");
    
    char count[32];
    snprintf(count, sizeof(count), "%d orders", selected_count);
    ssd1306_draw_text(4, 15, count);
    
    ssd1306_draw_text(6, 5, "Please wait...");
    
    force_redraw = true;
}

/* ---------- MAIN ---------- */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  RESTAURANT ORDER SYSTEM - CLIENT");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize hardware
    ESP_LOGI(TAG, "Initializing I2C and OLED...");
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_ERROR_CHECK(ssd1306_init_display());
    ssd1306_clear_screen();
    
    ssd1306_draw_text(2, 15, "STARTING...");
    ssd1306_draw_text(4, 5, "Connecting WiFi");
    
    buttons_init();
    ESP_LOGI(TAG, "Buttons initialized");
    
    // Initialize WiFi
    wifi_init_sta();
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    
    while (!wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ssd1306_clear_screen();
    ssd1306_draw_text(3, 10, "CONNECTED!");
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    ESP_LOGI(TAG, "System Ready!");
    force_redraw = true;
    
    // Button state tracking
    TickType_t last_scroll_press = 0;
    TickType_t last_select_press = 0;
    TickType_t first_select_press = 0;
    int select_press_count = 0;
    
    bool last_scroll_state = 1;
    bool last_select_state = 1;
    
    while (1) {
        TickType_t now = xTaskGetTickCount();
        int scroll_state = gpio_get_level(BTN_SCROLL);
        int select_state = gpio_get_level(BTN_SELECT);
        
        // ========== SCROLL BUTTON (Rising edge) ==========
        if (last_scroll_state == 0 && scroll_state == 1) {
            if ((now - last_scroll_press) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                selected_item = (selected_item + 1) % MENU_COUNT;
                ESP_LOGI(TAG, "Scrolled to item %d: %s", selected_item + 1, menu_items[selected_item]);
                last_scroll_press = now;
            }
        }
        last_scroll_state = scroll_state;
        
        // ========== SELECT BUTTON (Rising edge) ==========
        if (last_select_state == 0 && select_state == 1) {
            if ((now - last_select_press) * portTICK_PERIOD_MS > DEBOUNCE_DELAY_MS) {
                
                if (select_press_count == 0) {
                    // First press
                    first_select_press = now;
                    select_press_count = 1;
                    ESP_LOGD(TAG, "Select button: First press");
                    
                } else if (select_press_count == 1) {
                    // Second press - check timing
                    uint32_t time_diff = (now - first_select_press) * portTICK_PERIOD_MS;
                    
                    if (time_diff < DOUBLE_PRESS_WINDOW_MS) {
                        // *** DOUBLE PRESS DETECTED ***
                        ESP_LOGI(TAG, "=== DOUBLE PRESS - Sending all orders ===");
                        
                        display_sending();
                        vTaskDelay(pdMS_TO_TICKS(500));
                        
                        send_orders_to_host();
                        
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        select_press_count = 0;
                        
                    } else {
                        // Too slow - treat as new first press
                        first_select_press = now;
                        select_press_count = 1;
                    }
                }
                
                last_select_press = now;
            }
        }
        last_select_state = select_state;
        
        // ========== CHECK SINGLE PRESS TIMEOUT ==========
        if (select_press_count == 1) {
            uint32_t elapsed = (now - first_select_press) * portTICK_PERIOD_MS;
            
            if (elapsed >= DOUBLE_PRESS_WINDOW_MS) {
                // *** SINGLE PRESS CONFIRMED - Toggle selection ***
                selected_flags[selected_item] = !selected_flags[selected_item];
                
                if (selected_flags[selected_item]) {
                    selected_count++;
                    ESP_LOGI(TAG, "ADDED: %s (Total: %d)", menu_items[selected_item], selected_count);
                    display_item_toggled(true);
                } else {
                    selected_count--;
                    ESP_LOGI(TAG, "REMOVED: %s (Total: %d)", menu_items[selected_item], selected_count);
                    display_item_toggled(false);
                }
                
                vTaskDelay(pdMS_TO_TICKS(1500));
                select_press_count = 0;
            }
        }
        
        // ========== UPDATE DISPLAY (ONLY IF CHANGED) ==========
        display_menu();
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
