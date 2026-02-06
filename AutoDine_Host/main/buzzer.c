#include "buzzer.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";

void buzzer_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(BUZZER_PIN, 0);
    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", BUZZER_PIN);
}

void buzzer_beep(uint32_t duration_ms) {
    gpio_set_level(BUZZER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(BUZZER_PIN, 0);
}

void buzzer_beep_order(void) {
    buzzer_beep(BUZZER_DURATION_MS);
    ESP_LOGI(TAG, "New order alert");
}

void buzzer_beep_bill(void) {
    buzzer_beep(BUZZER_BILL_DURATION_MS);
    ESP_LOGI(TAG, "Bill request alert");
}
