idf_component_register(
    SRCS 
        "main.c"
        "hardware.c"
        "network.c"
        "ui_display.c"
        "state_machine.c"
    INCLUDE_DIRS "."
    REQUIRES 
        driver 
        esp_wifi 
        esp_http_client 
        nvs_flash
        json
        esp_timer  
)
