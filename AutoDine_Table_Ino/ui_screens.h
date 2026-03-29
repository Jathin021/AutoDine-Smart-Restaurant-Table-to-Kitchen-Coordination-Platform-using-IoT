#pragma once
/* =====================================================================
 *  ui_screens.h — AutoDine V4.0  LVGL screens
 * ===================================================================== */
#include "lvgl.h"
#include "state_machine.h"

/* Initialise UI subsystem — call once after LVGL is ready */
void ui_init(void);

/* Show a specific screen (called by state machine) */
void ui_show_screen(app_state_t state);

/* Check deferred actions (call from main loop, inside lvgl_acquire) */
void ui_check_deferred(void);

/* ---- Per-screen update functions (safe to call from network task) -- */
/* Must only be called inside lvgl_acquire() / lvgl_release() block     */

/* Update WiFi indicator icon on current screen */
void ui_set_wifi_connected(bool connected);

/* Rebuild menu grid from JSON string (server response) */
void ui_menu_load(const char *menu_json);

/* Mark a menu item as unavailable */
void ui_menu_item_set_available(int item_id, bool available);

/* Update cart panel quantities after add/remove */
void ui_cart_refresh(void);

/* Update order timer on ORDER_PLACED screen */
void ui_order_set_wait_time(int minutes);

/* Trigger food-ready animation */
void ui_food_ready_show(void);

/* Show bill with break-down */
void ui_bill_load(const char *bill_json);

/* Update UPI amount on payment screen */
void ui_payment_set_amount(int paise);

/* Show QR code image from URL */
void ui_upi_show_qr(const char *qr_url);

/* UPI payment result */
void ui_upi_show_result(bool success);

/* Show feedback countdown */
void ui_feedback_set_countdown(int seconds);

/* Add a single item card to the menu grid (called during menu load) */
void ui_add_menu_card(int id, const char *name, const char *desc,
                      int price_rupees, bool is_veg, bool available);
