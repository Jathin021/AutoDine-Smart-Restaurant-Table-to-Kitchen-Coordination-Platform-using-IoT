#include "hardware.h"
#include "app_config.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "HARDWARE";

// OLED Buffer
static uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];

// 5x7 Font (ASCII 32-122)
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x56,0x20,0x50},{0x00,0x08,0x07,0x03,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x2A,0x1C,0x7F,0x1C,0x2A},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x00,0x60,0x60,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4D,0x33},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36},{0x46,0x49,0x49,0x29,0x1E},{0x00,0x00,0x14,0x00,0x00},
    {0x00,0x40,0x34,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x59,0x09,0x06},{0x3E,0x41,0x5D,0x59,0x4E},
    {0x7C,0x12,0x11,0x12,0x7C},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x41,0x51,0x73},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x1C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32},{0x03,0x01,0x7F,0x01,0x03},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},{0x61,0x59,0x49,0x4D,0x43},{0x00,0x7F,0x41,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x41,0x7F},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x03,0x07,0x08,0x00},{0x20,0x54,0x54,0x78,0x40},
    {0x7F,0x28,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x28},{0x38,0x44,0x44,0x28,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x00,0x08,0x7E,0x09,0x02},{0x18,0xA4,0xA4,0x9C,0x78},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x40,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x78,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0xFC,0x18,0x24,0x24,0x18},
    {0x18,0x24,0x24,0x18,0xFC},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x24},
    {0x04,0x04,0x3F,0x44,0x24},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x4C,0x90,0x90,0x90,0x7C},
    {0x44,0x64,0x54,0x4C,0x44}
};

// I2C Write Command
static void oled_write_cmd(uint8_t cmd) {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, 0x00, true);
    i2c_master_write_byte(h, cmd, true);
    i2c_master_stop(h);
    i2c_master_cmd_begin(OLED_I2C_NUM, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
}

// I2C Write Data
static void oled_write_data(uint8_t *data, size_t len) {
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, 0x40, true);
    i2c_master_write(h, data, len, true);
    i2c_master_stop(h);
    i2c_master_cmd_begin(OLED_I2C_NUM, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
}

void oled_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_PIN,
        .scl_io_num = OLED_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_FREQ_HZ
    };
    i2c_param_config(OLED_I2C_NUM, &conf);
    i2c_driver_install(OLED_I2C_NUM, conf.mode, 0, 0, 0);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    oled_write_cmd(0xAE);
    oled_write_cmd(0xD5); oled_write_cmd(0x80);
    oled_write_cmd(0xA8); oled_write_cmd(0x3F);
    oled_write_cmd(0xD3); oled_write_cmd(0x00);
    oled_write_cmd(0x40);
    oled_write_cmd(0x8D); oled_write_cmd(0x14);
    oled_write_cmd(0x20); oled_write_cmd(0x00);
    oled_write_cmd(0xA1);
    oled_write_cmd(0xC8);
    oled_write_cmd(0xDA); oled_write_cmd(0x12);
    oled_write_cmd(0x81); oled_write_cmd(0xFF);
    oled_write_cmd(0xD9); oled_write_cmd(0xF1);
    oled_write_cmd(0xDB); oled_write_cmd(0x40);
    oled_write_cmd(0xA4);
    oled_write_cmd(0xA6);
    oled_write_cmd(0xAF);
    
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

void oled_clear_buffer(void) {
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

void oled_display(void) {
    for (uint8_t page = 0; page < 8; page++) {
        oled_write_cmd(0xB0 + page);
        oled_write_cmd(0x00);
        oled_write_cmd(0x10);
        oled_write_data(&oled_buffer[OLED_WIDTH * page], OLED_WIDTH);
    }
}

void oled_write_string(const char *str, uint8_t x, uint8_t y, bool large) {
    uint8_t page = y / 8;
    
    while (*str && page < 8) {
        if (*str == '\n') {
            page += large ? 3 : 1;
            str++;
            continue;
        }
        
        uint8_t ch = (*str >= 32 && *str <= 122) ? (*str - 32) : 0;
        
        if (large) {
            for (int i = 0; i < 5; i++) {
                uint8_t col = font5x7[ch][i];
                for (int dx = 0; dx < 3; dx++) {
                    if (x + (i * 3) + dx >= OLED_WIDTH) break;
                    if (page < 8) {
                        uint8_t top = 0;
                        for (int b = 0; b < 3; b++) {
                            if (col & (1 << b)) top |= (0x07 << (b * 2));
                        }
                        oled_buffer[page * OLED_WIDTH + x + (i * 3) + dx] = top;
                    }
                    if (page + 1 < 8) {
                        uint8_t mid = 0;
                        if (col & 0x08) mid |= 0x03;
                        for (int b = 4; b < 6; b++) {
                            if (col & (1 << b)) mid |= (0x07 << ((b - 3) * 2));
                        }
                        oled_buffer[(page + 1) * OLED_WIDTH + x + (i * 3) + dx] = mid;
                    }
                    if (page + 2 < 8) {
                        uint8_t bot = 0;
                        for (int b = 6; b < 8; b++) {
                            if (col & (1 << b)) bot |= (0x03 << ((b - 6) * 2));
                        }
                        oled_buffer[(page + 2) * OLED_WIDTH + x + (i * 3) + dx] = bot;
                    }
                }
            }
            x += 18;
        } else {
            for (int i = 0; i < 5; i++) {
                if (x + i >= OLED_WIDTH || page >= 8) break;
                oled_buffer[page * OLED_WIDTH + x + i] = font5x7[ch][i];
            }
            x += 6;
        }
        str++;
    }
}

void oled_write_centered(const char *str, uint8_t y, bool large) {
    uint8_t char_width = large ? 18 : 6;
    uint8_t text_width = strlen(str) * char_width;
    if (text_width >= OLED_WIDTH) {
        oled_write_string(str, 0, y, large);
    } else {
        uint8_t x = (OLED_WIDTH - text_width) / 2;
        oled_write_string(str, x, y, large);
    }
}

void oled_draw_hline(uint8_t x, uint8_t y, uint8_t w) {
    uint8_t page = y / 8;
    uint8_t bit = y % 8;
    if (page >= 8) return;
    for (uint8_t i = x; i < x + w && i < OLED_WIDTH; i++) {
        oled_buffer[page * OLED_WIDTH + i] |= (1 << bit);
    }
}

void oled_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool filled) {
    for (uint8_t py = y; py < y + h && py < OLED_HEIGHT; py++) {
        for (uint8_t px = x; px < x + w && px < OLED_WIDTH; px++) {
            uint8_t page = py / 8;
            uint8_t bit = py % 8;
            bool is_edge = (px == x || px == x + w - 1 || py == y || py == y + h - 1);
            if (filled || is_edge) {
                oled_buffer[page * OLED_WIDTH + px] |= (1 << bit);
            }
        }
    }
}

void oled_draw_bitmap(const unsigned char *bitmap) {
    // Copy full 128x64 bitmap directly to OLED buffer
    memcpy(oled_buffer, bitmap, sizeof(oled_buffer));
}

// Button State
static uint32_t btn_last_press_time[4] = {0};
static bool btn_is_pressed[4] = {false};
static uint32_t btn_press_start_time[4] = {0};

void buttons_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = ((1ULL << BTN_UP_PIN) | (1ULL << BTN_DOWN_PIN) | 
                         (1ULL << BTN_OK_PIN) | (1ULL << BTN_BACK_PIN)),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);
}

button_event_t buttons_scan(void) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    gpio_num_t pins[4] = {BTN_UP_PIN, BTN_DOWN_PIN, BTN_OK_PIN, BTN_BACK_PIN};
    button_event_t events[4] = {BTN_EVENT_UP_SHORT, BTN_EVENT_DOWN_SHORT, 
                                 BTN_EVENT_OK_SHORT, BTN_EVENT_BACK_SHORT};
    
    for (int i = 0; i < 4; i++) {
        int level = gpio_get_level(pins[i]);
        
        if (level == 0 && !btn_is_pressed[i]) {
            if (now - btn_last_press_time[i] > BTN_DEBOUNCE_MS) {
                btn_is_pressed[i] = true;
                btn_press_start_time[i] = now;
            }
        }
        
        if (level == 1 && btn_is_pressed[i]) {
            uint32_t duration = now - btn_press_start_time[i];
            btn_is_pressed[i] = false;
            btn_last_press_time[i] = now;
            
            if (i == 3 && duration >= BTN_LONG_PRESS_MS) {
                return BTN_EVENT_BACK_LONG;
            }
            return events[i];
        }
    }
    
    for (int i = 0; i < 4; i++) {
        if (gpio_get_level(pins[i]) == 0 && now - btn_last_press_time[i] > BTN_DEBOUNCE_MS) {
            for (int j = 0; j < 4; j++) btn_last_press_time[j] = now;
            return BTN_EVENT_ANY_PRESSED;
        }
    }
    
    return BTN_EVENT_NONE;
}

void hardware_init(void) {
    oled_init();
    buttons_init();
}
