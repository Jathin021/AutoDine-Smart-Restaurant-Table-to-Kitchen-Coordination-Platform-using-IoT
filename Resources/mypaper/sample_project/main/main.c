#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "lvgl.h"

static const char *TAG = "TEST";

/* Pins */
#define LCD_PIN_PCLK    0
#define LCD_PIN_HSYNC  39
#define LCD_PIN_VSYNC  40
#define LCD_PIN_DE     41
#define LCD_PIN_D0     15
#define LCD_PIN_D1      7
#define LCD_PIN_D2      6
#define LCD_PIN_D3      5
#define LCD_PIN_D4      4
#define LCD_PIN_D5      9
#define LCD_PIN_D6     46
#define LCD_PIN_D7      3
#define LCD_PIN_D8      8
#define LCD_PIN_D9     16
#define LCD_PIN_D10     1
#define LCD_PIN_D11    14
#define LCD_PIN_D12    21
#define LCD_PIN_D13    47
#define LCD_PIN_D14    48
#define LCD_PIN_D15    45
#define LCD_BL_PIN      2
#define TOUCH_SDA      19
#define TOUCH_SCL      20

/* Timing — exact from official Arduino code */
#define LCD_H_RES              800
#define LCD_V_RES              480
#define LCD_PCLK_HZ            15000000
#define HSYNC_BACK_PORCH       40
#define HSYNC_FRONT_PORCH      40
#define HSYNC_PULSE_WIDTH      48
#define VSYNC_BACK_PORCH       13
#define VSYNC_FRONT_PORCH       1
#define VSYNC_PULSE_WIDTH      31

#define LVGL_BUF_LINES         48

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static SemaphoreHandle_t      s_mux   = NULL;

static lv_disp_drv_t      s_drv;
static lv_indev_drv_t     s_idrv;
static lv_disp_draw_buf_t s_buf;
static lv_color_t         s_draw_buf[LCD_H_RES * LVGL_BUF_LINES];

/* Flush — actively push pixels, same as pushImageDMA */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                     lv_color_t *color_p)
{
    esp_lcd_panel_draw_bitmap(s_panel,
        area->x1, area->y1,
        area->x2 + 1, area->y2 + 1,
        color_p);
    lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    if (!s_touch) { data->state = LV_INDEV_STATE_RELEASED; return; }

    esp_lcd_touch_read_data(s_touch);

    uint16_t tx[1], ty[1], ts[1];
    uint8_t  cnt = 0;
    esp_lcd_touch_get_coordinates(s_touch, tx, ty, ts, &cnt, 1);

    if (cnt > 0) {
        data->point.x = tx[0];
        data->point.y = ty[0];
        data->state   = LV_INDEV_STATE_PRESSED;
        ESP_LOGI(TAG, "TOUCH x=%d y=%d", tx[0], ty[0]);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}


static void tick_cb(void *arg) { (void)arg; lv_tick_inc(2); }

static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTakeRecursive(s_mux, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGiveRecursive(s_mux);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    /* NVS */
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES ||
        r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    s_mux = xSemaphoreCreateRecursiveMutex();

    /* GPIO38 HIGH — matches official Arduino code */
    gpio_config_t g38 = {
        .pin_bit_mask = 1ULL << 38,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&g38);
    gpio_set_level(38, 1);

    /* Backlight GPIO2 HIGH */
    gpio_config_t bl = {
        .pin_bit_mask = 1ULL << LCD_BL_PIN,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(LCD_BL_PIN, 1);
    ESP_LOGI(TAG, "Backlight ON");

    /* I2C for GT911 */
    i2c_config_t ic = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TOUCH_SDA,
        .scl_io_num       = TOUCH_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_NUM_0, &ic);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

    /* GT911 touch */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
    };
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr                    = 0x5D,
        .control_phase_bytes         = 1,
        .dc_bit_offset               = 0,
        .lcd_cmd_bits                = 16,
        .lcd_param_bits              = 8,
        .flags.disable_control_phase = 1,
    };
    esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &tp_io_cfg, &tp_io);
    esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
    ESP_LOGI(TAG, "GT911 OK");

    /* RGB panel — 1 FB + bounce buffer */
    esp_lcd_rgb_panel_config_t rgb = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz           = LCD_PCLK_HZ,
            .h_res             = LCD_H_RES,
            .v_res             = LCD_V_RES,
            .hsync_back_porch  = HSYNC_BACK_PORCH,
            .hsync_front_porch = HSYNC_FRONT_PORCH,
            .hsync_pulse_width = HSYNC_PULSE_WIDTH,
            .vsync_back_porch  = VSYNC_BACK_PORCH,
            .vsync_front_porch = VSYNC_FRONT_PORCH,
            .vsync_pulse_width = VSYNC_PULSE_WIDTH,
            .flags.pclk_active_neg = true,
        },
        .data_width            = 16,
        .num_fbs               = 1,
        .bounce_buffer_size_px = LCD_H_RES * 10,
        .hsync_gpio_num        = LCD_PIN_HSYNC,
        .vsync_gpio_num        = LCD_PIN_VSYNC,
        .de_gpio_num           = LCD_PIN_DE,
        .pclk_gpio_num         = LCD_PIN_PCLK,
        .data_gpio_nums = {
            LCD_PIN_D0,  LCD_PIN_D1,  LCD_PIN_D2,  LCD_PIN_D3,
            LCD_PIN_D4,  LCD_PIN_D5,  LCD_PIN_D6,  LCD_PIN_D7,
            LCD_PIN_D8,  LCD_PIN_D9,  LCD_PIN_D10, LCD_PIN_D11,
            LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,
        },
        .flags.fb_in_psram = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&rgb, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_LOGI(TAG, "RGB panel OK");

    /* Raw RED fill test — 3 seconds */
    void *fb = NULL;
    esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb);
    memset(fb, 0xF8, LCD_H_RES * LCD_V_RES * 2);
    ESP_LOGI(TAG, ">>> RED FILL — do you see RED on screen? <<<");
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Raw GREEN fill — 3 seconds */
    memset(fb, 0x07, LCD_H_RES * LCD_V_RES * 2);
    ESP_LOGI(TAG, ">>> GREEN FILL — do you see GREEN on screen? <<<");
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* LVGL init */
    lv_init();
    lv_disp_draw_buf_init(&s_buf, s_draw_buf, NULL,
                          LCD_H_RES * LVGL_BUF_LINES);

    lv_disp_drv_init(&s_drv);
    s_drv.hor_res      = LCD_H_RES;
    s_drv.ver_res      = LCD_V_RES;
    s_drv.flush_cb     = flush_cb;
    s_drv.draw_buf     = &s_buf;
    s_drv.full_refresh = 0;
    s_drv.direct_mode  = 0;
    lv_disp_drv_register(&s_drv);

    lv_indev_drv_init(&s_idrv);
    s_idrv.type    = LV_INDEV_TYPE_POINTER;
    s_idrv.read_cb = touch_cb;
    lv_indev_drv_register(&s_idrv);

    /* Screen */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "SCREEN WORKS!\nTouch anywhere");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_center(label);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 220, 70);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_t *bl2 = lv_label_create(btn);
    lv_label_set_text(bl2, "TAP ME");
    lv_obj_center(bl2);

    /* LVGL tick timer */
    const esp_timer_create_args_t ta = {
        .callback = tick_cb, .name = "lvgl_tick"
    };
    esp_timer_handle_t th;
    esp_timer_create(&ta, &th);
    esp_timer_start_periodic(th, 2000);

    /* LVGL task on CPU1 */
    xTaskCreatePinnedToCore(lvgl_task, "lvgl",
                            16384, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, ">>> LVGL running — watch for white text on black <<<");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
