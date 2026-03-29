idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
        driver
        nvs_flash
        esp_timer
        freertos
        lvgl
)

target_compile_options(__idf_main PRIVATE -Wno-deprecated-declarations)
