#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"

// WiFi AP Configuration
#define WIFI_AP_SSID "AutoDine_Host"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONNECTIONS 4

// HTTP Server
#define HTTP_SERVER_PORT 80

// Buzzer
#define BUZZER_PIN GPIO_NUM_23
#define BUZZER_DURATION_MS 2000
#define BUZZER_BILL_DURATION_MS 3000

// Tables
#define MAX_TABLES 2
#define TABLE_1_ID 1
#define TABLE_2_ID 2

// Order Management
#define MAX_ORDERS 10
#define MAX_ORDER_ITEMS 20

#endif
