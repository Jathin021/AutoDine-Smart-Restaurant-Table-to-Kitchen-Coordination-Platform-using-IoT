/*
 * AutoDine_Table_Ino.ino — AutoDine V4.0 Table Firmware
 * Arduino IDE + LovyanGFX + LVGL + GT911 Touch
 *
 * REQUIRED: Download Elecrow pre-configured libraries from:
 * https://www.elecrow.com/download/product/CrowPanel/ESP32-HMI/7.0-DIS08070H/Arduino_Tutorial/libraries.zip
 * Extract contents to your Arduino/libraries folder.
 *
 * Board Settings (Tools menu):
 *   Board:      ESP32S3 Dev Module
 *   PSRAM:      OPI PSRAM            ← CRITICAL
 *   CPU Freq:   240MHz (WiFi)
 *   Flash Mode: QIO 80MHz
 *   Flash Size: 4MB (32Mb)
 *   Partition:  Huge APP (3MB No OTA / 1MB SPIFFS)
 *   USB Mode:   Hardware CDC and JTAG
 */

/* ─── Display (LovyanGFX) ──────────────────────────────────────────────── */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lvgl.h>
#include <Wire.h>
#include <WiFi.h>
#include <freertos/semphr.h>

/* ─── AutoDine source files ─────────────────────────────────────────────── */
extern "C" {
#include "app_config.h"
#include "cart.h"
#include "state_machine.h"
#include "ui_screens.h"
#include "autodine_net.h"
}

/* ─── LGFX class (proven working from Elecrow color test) ──────────────── */
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB   _bus_instance;
  lgfx::Panel_RGB _panel_instance;

  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      cfg.pin_d0  = GPIO_NUM_15;  cfg.pin_d1  = GPIO_NUM_7;
      cfg.pin_d2  = GPIO_NUM_6;   cfg.pin_d3  = GPIO_NUM_5;
      cfg.pin_d4  = GPIO_NUM_4;   cfg.pin_d5  = GPIO_NUM_9;
      cfg.pin_d6  = GPIO_NUM_46;  cfg.pin_d7  = GPIO_NUM_3;
      cfg.pin_d8  = GPIO_NUM_8;   cfg.pin_d9  = GPIO_NUM_16;
      cfg.pin_d10 = GPIO_NUM_1;   cfg.pin_d11 = GPIO_NUM_14;
      cfg.pin_d12 = GPIO_NUM_21;  cfg.pin_d13 = GPIO_NUM_47;
      cfg.pin_d14 = GPIO_NUM_48;  cfg.pin_d15 = GPIO_NUM_45;

      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = 15000000;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch  = 40;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch  = 13;
      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _bus_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

static LGFX lcd;

/* ─── Touch (official Elecrow touch.h / TAMC_GT911) ────────────────────── */
#include "touch.h"

/* ─── LVGL draw buffer ── 800×40 partial (official pattern) ────────────── */
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         disp_buf[800 * 40];

/* ─── LVGL mutex (used by ui_screens.c and state_machine.c) ────────────── */
static SemaphoreHandle_t _lvgl_mux = NULL;

extern "C" void lvgl_acquire(void) {
  xSemaphoreTakeRecursive(_lvgl_mux, portMAX_DELAY);
}
extern "C" void lvgl_release(void) {
  xSemaphoreGiveRecursive(_lvgl_mux);
}

/* ─── LVGL flush (official Elecrow pattern: pushImageDMA) ──────────────── */
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area,
                           lv_color_t *color_p)
{
  uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  lcd.pushImageDMA(area->x1, area->y1, w, h,
                   (lgfx::rgb565_t *)&color_p->full);
#else
  lcd.pushImageDMA(area->x1, area->y1, w, h,
                   (lgfx::rgb565_t *)&color_p->full);
#endif

  lv_disp_flush_ready(disp);
}

/* ─── LVGL touch read ───────────────────────────────────────────────────── */
static void my_touchpad_read(lv_indev_drv_t *indev_driver,
                              lv_indev_data_t *data)
{
  if (touch_has_signal()) {
    if (touch_touched()) {
      data->state   = LV_INDEV_STATE_PR;
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    } else if (touch_released()) {
      data->state = LV_INDEV_STATE_REL;
    }
  } else {
  }
}

/* ─── WiFi + first menu fetch ───────────────────────────────────────────── */
static bool s_wifi_done    = false;
static bool s_menu_fetched = false;
static unsigned long s_wifi_start = 0;

/* ══════════════════════════════════════════════════════════════════════════ */
void setup()
{
  Serial.begin(115200);
  Serial.println("AutoDine V4.0 starting...");

  /* GPIO38 HIGH — must be set before lcd.begin() */
  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);

  /* Backlight */
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);

  /* Display */
  lcd.begin();
  lcd.fillScreen(TFT_BLACK);

  /* Touch */
  touch_init();

  /* LVGL mutex + init */
  _lvgl_mux = xSemaphoreCreateRecursiveMutex();
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, disp_buf, NULL, 800 * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = 800;
  disp_drv.ver_res  = 480;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  /* App init */
  sm_init();
  cart_clear();
  lvgl_acquire();
  ui_init();
  ui_show_screen(STATE_SPLASH);
  lvgl_release();

  /* Start WiFi (non-blocking — checked in loop) */
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  s_wifi_start = millis();
  Serial.println("Setup done — entering loop");
}

/* ══════════════════════════════════════════════════════════════════════════ */
void loop()
{
  /* === LVGL tick + handler (official pattern) */
  /* LVGL tick is driven by LV_TICK_CUSTOM (millis) in lv_conf.h */
  lvgl_acquire();
  lv_timer_handler();
  sm_update();
  lvgl_release();

  /* Handle blocking network calls (UPI/Append) outside the mutex */
  ui_check_deferred();

  /* === WiFi connection check (non-blocking, first 10s) */
  if (!s_wifi_done && (millis() - s_wifi_start < 10000)) {
    if (WiFi.status() == WL_CONNECTED) {
      s_wifi_done = true;
      Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
      lvgl_acquire();
      ui_set_wifi_connected(true);
      lvgl_release();
    }
  }

  /* === Fetch menu once after WiFi is up */
  if (s_wifi_done && !s_menu_fetched) {
    s_menu_fetched = true;   /* set first so we don't retry on failure */
    char *json = net_fetch_menu();
    if (json) {
      lvgl_acquire();
      ui_menu_load(json);
      lvgl_release();
      free(json);
    }
  }

  delay(5);
}
