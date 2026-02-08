#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"

// TABLE IDENTITY
#define TABLE_ID 2
#define TABLE_NAME "Table-2"

// NETWORK
#define WIFI_SSID "AutoDine_Host"
#define WIFI_PASSWORD "12345678"
#define WIFI_MAX_RETRY 10
#define HOST_IP "192.168.4.1"
#define HOST_PORT 80

// API ENDPOINTS
#define API_ORDER "/api/order"
#define API_TABLE_STATUS "/api/table_status"
#define API_REQUEST_BILL "/api/request_bill"
#define API_PAYMENT "/api/payment"

// HARDWARE - OLED
#define OLED_I2C_NUM I2C_NUM_0
#define OLED_SDA_PIN GPIO_NUM_21
#define OLED_SCL_PIN GPIO_NUM_22
#define OLED_I2C_FREQ_HZ 400000
#define OLED_I2C_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// HARDWARE - BUTTONS
#define BTN_UP_PIN GPIO_NUM_32
#define BTN_DOWN_PIN GPIO_NUM_33
#define BTN_OK_PIN GPIO_NUM_25
#define BTN_BACK_PIN GPIO_NUM_26
#define BTN_DEBOUNCE_MS 50
#define BTN_LONG_PRESS_MS 3000

// TIMING
#define STATUS_POLL_INTERVAL_MS 1000
#define DISPLAY_MESSAGE_DURATION_MS 4000

// CART
#define MAX_CART_ITEMS 20
#define MAX_ITEM_NAME_LEN 32

#endif
