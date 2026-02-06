#include "ui_display.h"
#include "hardware.h"
#include "app_config.h"
#include "menu_data.h"
#include "cjson.h"
#include <stdio.h>
#include <string.h>

void ui_display_idle(void) {
    oled_clear_buffer();
    oled_write_centered("AutoDine", 18, false);
    oled_draw_hline(25, 38, 78);
    oled_write_centered("Press any", 44, false);
    oled_write_centered("button to order", 54, false);
    oled_display();
}

void ui_display_menu(uint8_t menu_index, cart_t *cart) {
    oled_clear_buffer();
    
    // Item counter (top-left, small)
    char num[8];
    snprintf(num, sizeof(num), "%d/%d", menu_index + 1, MENU_ITEMS_COUNT);
    oled_write_string(num, 2, 1, false);
    
    // ✅ SMALLER FONT - fits up to 14 characters
    char name[16];
    snprintf(name, sizeof(name), "%.14s", MENU_DATABASE[menu_index].name);
    oled_write_centered(name, 20, false);  // Small font
    
    // Price
    char price[12];
    snprintf(price, sizeof(price), "Rs %d", MENU_DATABASE[menu_index].price);
    oled_write_centered(price, 38, false);
    
    // Cart summary (bottom)
    if (!cart_is_empty(cart)) {
        char cart_txt[22];
        snprintf(cart_txt, sizeof(cart_txt), "Cart:%d Rs%lu", 
                cart->count, (unsigned long)cart->total);
        oled_write_string(cart_txt, 2, 56, false);
    }
    
    oled_display();
}

void ui_display_quantity(uint8_t menu_index, uint8_t qty) {
    oled_clear_buffer();
    
    char name[22];
    snprintf(name, sizeof(name), "%.20s", MENU_DATABASE[menu_index].name);
    oled_write_string(name, 2, 1, false);
    oled_draw_hline(0, 10, 128);
    
    char qty_str[4];
    snprintf(qty_str, sizeof(qty_str), "%d", qty);
    oled_write_centered(qty_str, 20, true);
    
    oled_write_string("OK=Add X=Back", 12, 56, false);
    
    oled_display();
}

void ui_display_waiting_order(void) {
    oled_clear_buffer();
    oled_write_centered("Sending", 16, true);
    oled_write_centered("Order...", 40, false);
    oled_display();
}

void ui_display_order_declined(void) {
    oled_clear_buffer();
    oled_draw_rect(10, 10, 108, 44, false);
    oled_write_centered("SORRY!", 18, true);
    oled_write_centered("Order is", 40, false);
    oled_write_centered("currently", 48, false);
    oled_write_centered("unavailable", 56, false);
    oled_display();
}

void ui_display_cooking(void) {
    oled_clear_buffer();
    oled_write_centered("Food is", 8, false);
    oled_write_centered("preparing in", 20, false);
    oled_write_centered("10-15 minutes", 32, false);
    oled_draw_hline(0, 48, 128);
    // ✅ FIX: Add small text hint
    oled_write_string("Bt-3->Add more", 16, 54, false);
    oled_display();
}

void ui_display_food_prepared(void) {
    oled_clear_buffer();
    oled_write_centered("Food is", 8, true);
    oled_write_centered("prepared!", 32, true);
    oled_write_centered("Enjoy the meal", 46, false);
    oled_draw_hline(0, 52, 128);
    // ✅ FIX: Correct format with arrows
    oled_write_string("Bt-3->More Bt-4->Bill", 8, 56, false);
    oled_display();
}

// Add after ui_display_order_declined()
void ui_display_order_declined_append(void) {
    oled_clear_buffer();
    oled_draw_rect(10, 10, 108, 44, false);
    oled_write_centered("Items not", 24, true);
    oled_write_centered("available", 42, false);
    oled_display();
}

void ui_display_waiting_bill(void) {
    oled_clear_buffer();
    oled_write_centered("Waiting for", 18, true);
    oled_write_centered("bill", 42, false);
    oled_display();
}

void ui_display_bill(const char *bill_json, uint8_t scroll_index) {
    oled_clear_buffer();
    
    oled_draw_rect(0, 0, 128, 10, true);
    oled_write_centered("BILL", 2, false);
    
    if (!bill_json || strlen(bill_json) == 0) {
        oled_write_centered("No data", 28, false);
        oled_display();
        return;
    }
    
    cJSON *root = cJSON_Parse(bill_json);
    if (!root) {
        oled_write_centered("Error", 28, false);
        oled_display();
        return;
    }
    
    uint8_t y = 13;
    uint8_t line = 0;
    
    cJSON *items = cJSON_GetObjectItem(root, "items");
    if (items && cJSON_IsArray(items)) {
        int count = cJSON_GetArraySize(items);
        for (int i = 0; i < count; i++) {
            if (line >= scroll_index && line < scroll_index + 3 && y < 45) {
                cJSON *item = cJSON_GetArrayItem(items, i);
                cJSON *name = cJSON_GetObjectItem(item, "name");
                cJSON *qty = cJSON_GetObjectItem(item, "qty");
                cJSON *sub = cJSON_GetObjectItem(item, "subtotal");
                
                char buf[22];
                snprintf(buf, 14, "%.12s", name ? name->valuestring : "?");
                oled_write_string(buf, 2, y, false);
                
                snprintf(buf, sizeof(buf), "x%d", qty ? qty->valueint : 0);
                oled_write_string(buf, 86, y, false);
                
                snprintf(buf, sizeof(buf), "%d", sub ? sub->valueint : 0);
                uint8_t price_x = 122 - (strlen(buf) * 6);
                oled_write_string(buf, price_x, y, false);
                
                y += 11;
            }
            line++;
        }
    }
    
    oled_draw_hline(0, 52, 128);
    oled_draw_rect(0, 53, 128, 11, true);
    
    cJSON *grand = cJSON_GetObjectItem(root, "grand_total");
    char total[22];
    snprintf(total, sizeof(total), "TOTAL:Rs%.0f", grand ? grand->valuedouble : 0);
    oled_write_string(total, 4, 55, false);
    
    cJSON_Delete(root);
    oled_display();
}

void ui_display_payment_method(void) {
    oled_clear_buffer();
    oled_write_centered("Payment", 6, true);
    oled_draw_hline(10, 32, 108);
    oled_write_centered("1. UPI/QR", 38, false);
    oled_write_centered("2. Cash/Card", 52, false);
    oled_display();
}

void ui_display_payment_qr_upi(void) {
    oled_clear_buffer();
    oled_write_centered("UPI Payment", 1, false);
    oled_draw_hline(10, 10, 108);
    
    oled_draw_rect(32, 14, 64, 64, false);
    oled_draw_rect(34, 16, 60, 60, false);
    
    oled_write_centered("Scan & Pay", 56, false);
    oled_display();
}

void ui_display_payment_cash(void) {
    oled_clear_buffer();
    oled_draw_rect(8, 12, 112, 40, false);
    oled_draw_rect(10, 14, 108, 36, false);
    oled_write_centered("Pay at", 22, true);
    oled_write_centered("Counter", 46, false);
    oled_display();
}

void ui_display_thank_you(void) {
    oled_clear_buffer();
    oled_draw_rect(8, 8, 112, 48, false);
    oled_write_centered("THANK", 16, true);
    oled_write_centered("YOU!", 40, true);
    oled_display();
}

void ui_render_current_state(state_context_t *ctx, cart_t *cart, const char *bill_data) {
    switch (ctx->current) {
        case STATE_IDLE: 
            ui_display_idle(); 
            break;
        case STATE_MENU_BROWSE: 
            ui_display_menu(ctx->menu_index, cart); 
            break;
        case STATE_QUANTITY_SELECT: 
            ui_display_quantity(ctx->menu_index, ctx->quantity); 
            break;
        case STATE_WAITING_ORDER_ACCEPT: 
            ui_display_waiting_order(); 
            break;
        case STATE_ORDER_DECLINED: 
            ui_display_order_declined(); 
            break;
        case STATE_ORDER_DECLINED_APPEND:  // ✅ ADD THIS CASE
            ui_display_order_declined_append();
            break;
        case STATE_COOKING: 
            ui_display_cooking(); 
            break;
        case STATE_FOOD_PREPARED: 
            ui_display_food_prepared(); 
            break;
        case STATE_WAITING_BILL: 
            ui_display_waiting_bill(); 
            break;
        case STATE_BILL_DISPLAY: 
            ui_display_bill(bill_data, ctx->bill_scroll_index); 
            break;
        case STATE_PAYMENT_METHOD_SELECT: 
            ui_display_payment_method(); 
            break;
        case STATE_PAYMENT_QR_UPI: 
            ui_display_payment_qr_upi(); 
            break;
        case STATE_PAYMENT_CASH: 
            ui_display_payment_cash(); 
            break;
        case STATE_THANK_YOU: 
            ui_display_thank_you(); 
            break;
        default: 
            break;
    }
}
