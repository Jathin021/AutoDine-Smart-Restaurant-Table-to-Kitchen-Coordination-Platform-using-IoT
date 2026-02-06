#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "wifi_ap.h"
#include "http_server.h"
#include "order_manager.h"
#include "buzzer.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "AutoDine Host Unit Starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    buzzer_init();
    
    order_manager_init();
    
    wifi_init_softap();
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    httpd_handle_t server = http_server_start();
    
    if (server) {
        ESP_LOGI(TAG, "===========================================");
        ESP_LOGI(TAG, "AutoDine Host Ready");
        ESP_LOGI(TAG, "WiFi SSID: %s", WIFI_AP_SSID);
        ESP_LOGI(TAG, "WiFi Password: %s", WIFI_AP_PASSWORD);
        ESP_LOGI(TAG, "Dashboard URL: http://192.168.4.1");
        ESP_LOGI(TAG, "===========================================");
    } else {
        ESP_LOGE(TAG, "Failed to start server");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
