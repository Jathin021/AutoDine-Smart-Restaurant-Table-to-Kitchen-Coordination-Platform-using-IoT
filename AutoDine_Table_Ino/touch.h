/*
 * touch.h — Official Elecrow-style GT911 touch for CrowPanel 7.0 (800x480)
 * Uses TAMC_GT911 library (gt911-arduino-main — already ESP32 core 3.x compatible)
 *
 * Install via Arduino Library Manager or from:
 *   Documents/Arduino/libraries/gt911-arduino-main
 */
#pragma once
#include <TAMC_GT911.h>

/* ── CrowPanel 7.0 pin mapping ─────────────────────────────────────── */
#define TOUCH_GT911_SDA   19
#define TOUCH_GT911_SCL   20
#define TOUCH_GT911_INT   255   /* not connected — 255 (= uint8_t(-1)) no-ops safely */
#define TOUCH_GT911_RST   38
#define TOUCH_MAP_X1      800
#define TOUCH_MAP_Y1      480

/* ── LVGL-readable coordinates ─────────────────────────────────────── */
int touch_last_x = 0;
int touch_last_y = 0;

/* ── TAMC_GT911 instance ────────────────────────────────────────────── */
TAMC_GT911 TS = TAMC_GT911(TOUCH_GT911_SDA, TOUCH_GT911_SCL,
                             TOUCH_GT911_INT, TOUCH_GT911_RST,
                             TOUCH_MAP_X1, TOUCH_MAP_Y1);

/* ── Public API ─────────────────────────────────────────────────────── */
void touch_init() {
    TS.begin();
    TS.setRotation(ROTATION_INVERTED);  /* raw coords = screen coords for landscape */
}

bool touch_has_signal() { return true; }

bool touch_touched() {
    TS.read();
    if (TS.isTouched) {
        touch_last_x = TS.points[0].x;
        touch_last_y = TS.points[0].y;
        return true;
    }
    return false;
}

bool touch_released() { return !TS.isTouched; }
