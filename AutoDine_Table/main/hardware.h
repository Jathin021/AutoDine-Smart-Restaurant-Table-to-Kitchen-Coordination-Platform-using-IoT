#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>
#include <stdbool.h>

// Button Events
typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_UP_SHORT,
    BTN_EVENT_DOWN_SHORT,
    BTN_EVENT_OK_SHORT,
    BTN_EVENT_BACK_SHORT,
    BTN_EVENT_BACK_LONG,
    BTN_EVENT_ANY_PRESSED
} button_event_t;

// OLED Functions
void oled_init(void);
void oled_clear_buffer(void);
void oled_display(void);
void oled_write_string(const char *str, uint8_t x, uint8_t y, bool large);
void oled_write_centered(const char *str, uint8_t y, bool large);
void oled_draw_hline(uint8_t x, uint8_t y, uint8_t w);
void oled_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool filled);
void oled_draw_bitmap(const unsigned char *bitmap);

// Button Functions
void buttons_init(void);
button_event_t buttons_scan(void);

// Hardware Init
void hardware_init(void);

#endif
