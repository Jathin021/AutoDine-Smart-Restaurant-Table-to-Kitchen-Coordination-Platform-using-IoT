#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdint.h>
#include "state_machine.h"
#include "menu_data.h"

void ui_display_idle(void);
void ui_display_menu(uint8_t menu_index, cart_t *cart);
void ui_display_quantity(uint8_t menu_index, uint8_t qty);
void ui_display_waiting_order(void);
void ui_display_order_declined(void);
void ui_display_cooking(void);
// Add in ui_display.h after line 15
void ui_display_order_declined_append(void);
void ui_display_food_prepared(void);
void ui_display_waiting_bill(void);
void ui_display_bill(const char *bill_json, uint8_t scroll_index);
void ui_display_payment_method(void);
void ui_display_payment_qr_upi(void);
void ui_display_payment_cash(void);
void ui_display_thank_you(void);
void ui_render_current_state(state_context_t *ctx, cart_t *cart, const char *bill_data);

#endif
