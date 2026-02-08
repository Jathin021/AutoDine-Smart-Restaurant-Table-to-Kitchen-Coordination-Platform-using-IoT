#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "app_config.h"
#include "hardware.h"
#include "state_machine.h"
#include "network.h"
#include "ui_display.h"
#include "menu_data.h"

static const char *TAG = "MAIN";

// Global State
static state_context_t state_ctx;
static cart_t accepted_cart;  // Items already accepted by chef
static cart_t pending_cart;   // Items being added (in append mode or initial order)
static char bill_json_buffer[2048] = {0};
static bool network_ready = false;

// Forward declarations
static void handle_state_idle(button_event_t btn);
static void handle_state_menu_browse(button_event_t btn);
static void handle_state_quantity_select(button_event_t btn);
static void handle_state_waiting_order_accept(button_event_t btn);
static void handle_state_order_declined(button_event_t btn);
static void handle_state_cooking(button_event_t btn);
static void handle_state_food_prepared(button_event_t btn);
static void handle_state_waiting_bill(button_event_t btn);
static void handle_state_bill_display(button_event_t btn);
static void handle_state_payment_method_select(button_event_t btn);
static void handle_state_payment_qr_upi(button_event_t btn);
static void handle_state_payment_cash(button_event_t btn);
static void handle_state_thank_you(button_event_t btn);
static void poll_server_status(void);

void app_main(void) {
    ESP_LOGI(TAG, "AutoDine Table Unit - %s", TABLE_NAME);
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize Hardware
    hardware_init();
    
    // Initialize State Machine
    state_machine_init(&state_ctx);
    cart_init(&accepted_cart);
    cart_init(&pending_cart);
    
    // Show connecting screen
    oled_clear_buffer();
    oled_write_centered("Connecting", 20, false);
    oled_write_centered("to server...", 35, false);
    oled_display();
    
    // Initialize Network
    network_wifi_init();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if (network_wifi_is_connected()) {
        network_ready = true;
        ESP_LOGI(TAG, "Network ready");
    } else {
        ESP_LOGE(TAG, "Network connection failed");
        oled_clear_buffer();
        oled_write_centered("ERROR", 20, true);
        oled_write_centered("No WiFi", 45, false);
        oled_display();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    
    // Show initial idle screen
    ui_render_current_state(&state_ctx, state_ctx.append_mode ? &pending_cart : &accepted_cart, bill_json_buffer);
    
    uint32_t last_poll_time = 0;
    uint32_t last_render_time = 0;
    
    // Main Loop
    while (1) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        
        // Scan buttons
        button_event_t btn = buttons_scan();
        
        // Handle button events based on current state
        switch (state_ctx.current) {
            case STATE_IDLE:
                handle_state_idle(btn);
                break;
            case STATE_MENU_BROWSE:
                handle_state_menu_browse(btn);
                break;
            case STATE_QUANTITY_SELECT:
                handle_state_quantity_select(btn);
                break;
            case STATE_WAITING_ORDER_ACCEPT:
                handle_state_waiting_order_accept(btn);
                break;
            case STATE_ORDER_DECLINED:
                handle_state_order_declined(btn);
                break;
            case STATE_COOKING:
                handle_state_cooking(btn);
                break;
            case STATE_FOOD_PREPARED:
                handle_state_food_prepared(btn);
                break;
            case STATE_WAITING_BILL:
                handle_state_waiting_bill(btn);
                break;
            case STATE_BILL_DISPLAY:
                handle_state_bill_display(btn);
                break;
            case STATE_PAYMENT_METHOD_SELECT:
                handle_state_payment_method_select(btn);
                break;
            case STATE_PAYMENT_QR_UPI:
                handle_state_payment_qr_upi(btn);
                break;
            case STATE_PAYMENT_CASH:
                handle_state_payment_cash(btn);
                break;
            case STATE_THANK_YOU:
                handle_state_thank_you(btn);
                break;
            default:
                break;
        }
        
        // Poll server for status updates
        if (network_ready && (now - last_poll_time) >= STATUS_POLL_INTERVAL_MS) {
            poll_server_status();
            last_poll_time = now;
        }
        
        // Auto-transition after timeout for certain states
        uint32_t time_in_state = state_machine_time_in_state(&state_ctx);
        
        if (state_ctx.current == STATE_ORDER_DECLINED &&
    time_in_state >= DISPLAY_MESSAGE_DURATION_MS) {
    state_machine_transition(&state_ctx, STATE_IDLE);
    cart_init(&accepted_cart);
    cart_init(&pending_cart);
    ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
}

// ✅ ADD NEW CASE for append-mode decline
if (state_ctx.current == STATE_ORDER_DECLINED_APPEND &&
    time_in_state >= 3000) {  // 3 sec timeout
    // Return to FOOD_PREPARED, keep cart
    state_machine_transition(&state_ctx, STATE_FOOD_PREPARED);
    state_ctx.append_mode = false;  // Reset append mode
    ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
}

        
        if (state_ctx.current == STATE_THANK_YOU && 
            time_in_state >= DISPLAY_MESSAGE_DURATION_MS) {
            state_machine_transition(&state_ctx, STATE_IDLE);
            cart_init(&accepted_cart);
            cart_init(&pending_cart);
            memset(bill_json_buffer, 0, sizeof(bill_json_buffer));
            ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
        }
        
        // Re-render periodically (for blinking effects, etc.)
        if ((now - last_render_time) >= 500) {
            // Optional: Add visual feedback
            last_render_time = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================
// STATE HANDLERS
// ============================================

static void handle_state_idle(button_event_t btn) {
    if (btn == BTN_EVENT_ANY_PRESSED) {
        state_machine_transition(&state_ctx, STATE_MENU_BROWSE);
        state_ctx.menu_index = 0;
        state_ctx.append_mode = false;
        ui_render_current_state(&state_ctx, &pending_cart, bill_json_buffer);
    }
}

static void handle_state_menu_browse(button_event_t btn) {
    bool redraw = false;
    
    if (btn == BTN_EVENT_UP_SHORT) {
        if (state_ctx.menu_index > 0) {
            state_ctx.menu_index--;
            redraw = true;
        }
    } else if (btn == BTN_EVENT_DOWN_SHORT) {
        if (state_ctx.menu_index < MENU_ITEMS_COUNT - 1) {
            state_ctx.menu_index++;
            redraw = true;
        }
    } else if (btn == BTN_EVENT_OK_SHORT) {
        state_machine_transition(&state_ctx, STATE_QUANTITY_SELECT);
        state_ctx.quantity = 1;
        redraw = true;
    } else if (btn == BTN_EVENT_BACK_SHORT) {
        if (!cart_is_empty(&pending_cart)) {
            // Send order (pending cart only)
            state_machine_transition(&state_ctx, STATE_WAITING_ORDER_ACCEPT);
            ui_render_current_state(&state_ctx, &pending_cart, bill_json_buffer);
            
            if (network_send_order(TABLE_ID, &pending_cart, state_ctx.append_mode)) {
                ESP_LOGI(TAG, "Order sent successfully");
            } else {
                ESP_LOGE(TAG, "Failed to send order");
                state_machine_transition(&state_ctx, STATE_ORDER_DECLINED);
                ui_render_current_state(&state_ctx, &pending_cart, bill_json_buffer);
            }
            return;
        } else {
            state_machine_transition(&state_ctx, STATE_IDLE);
            redraw = true;
        }
} else if (btn == BTN_EVENT_BACK_LONG) {
    if (state_ctx.append_mode) {
        // ✅ FIX: Use previous state, not hardcoded
        state_machine_transition(&state_ctx, state_ctx.previous);
        state_ctx.append_mode = false;
        redraw = true;
    }
}
    if (redraw) {
        ui_render_current_state(&state_ctx, state_ctx.append_mode ? &pending_cart : &accepted_cart, bill_json_buffer);
    }
}

static void handle_state_quantity_select(button_event_t btn) {
    bool redraw = false;
    
    if (btn == BTN_EVENT_DOWN_SHORT) {
        if (state_ctx.quantity < 15) {
            state_ctx.quantity++;
            redraw = true;
        }
    } else if (btn == BTN_EVENT_UP_SHORT) {
        if (state_ctx.quantity > 0) {
            state_ctx.quantity--;
            redraw = true;
        }
    } else if (btn == BTN_EVENT_OK_SHORT) {
        if (state_ctx.quantity > 0) {
            cart_add_item(&pending_cart, 
                         MENU_DATABASE[state_ctx.menu_index].item_id,
                         MENU_DATABASE[state_ctx.menu_index].name,
                         MENU_DATABASE[state_ctx.menu_index].price,
                         state_ctx.quantity);
            ESP_LOGI(TAG, "Added to cart: %s x%d", 
                    MENU_DATABASE[state_ctx.menu_index].name, state_ctx.quantity);
        }
        state_machine_transition(&state_ctx, STATE_MENU_BROWSE);
        redraw = true;
    } else if (btn == BTN_EVENT_BACK_SHORT) {
        state_machine_transition(&state_ctx, STATE_MENU_BROWSE);
        redraw = true;
    }
    
    if (redraw) {
        ui_render_current_state(&state_ctx, &pending_cart, bill_json_buffer);
    }
}

static void handle_state_waiting_order_accept(button_event_t btn) {
    // Handled by server polling
}

static void handle_state_order_declined(button_event_t btn) {
    // Auto-transitions after timeout
}

static void handle_state_cooking(button_event_t btn) {
    if (btn == BTN_EVENT_OK_SHORT) {
        // Add more items - use pending cart
        state_ctx.append_mode = true;
        cart_init(&pending_cart);  // Clear pending cart for new items
        state_machine_transition(&state_ctx, STATE_MENU_BROWSE);
        state_ctx.menu_index = 0;
        ui_render_current_state(&state_ctx, &pending_cart, bill_json_buffer);
    }
}

static void handle_state_food_prepared(button_event_t btn) {
    if (btn == BTN_EVENT_OK_SHORT) {
        // Add more items - use pending cart
        state_ctx.append_mode = true;
        cart_init(&pending_cart);  // Clear pending cart for new items
        state_machine_transition(&state_ctx, STATE_MENU_BROWSE);
        state_ctx.menu_index = 0;
        ui_render_current_state(&state_ctx, &pending_cart, bill_json_buffer);
    } else if (btn == BTN_EVENT_BACK_SHORT) {
        // Request bill
        state_machine_transition(&state_ctx, STATE_WAITING_BILL);
        ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
        
        if (network_request_bill(TABLE_ID)) {
            ESP_LOGI(TAG, "Bill requested");
        } else {
            ESP_LOGE(TAG, "Failed to request bill");
        }
    }
}

static void handle_state_waiting_bill(button_event_t btn) {
    // Handled by server polling
}

static void handle_state_bill_display(button_event_t btn) {
    bool redraw = false;
    
    if (btn == BTN_EVENT_UP_SHORT || btn == BTN_EVENT_DOWN_SHORT || 
        btn == BTN_EVENT_OK_SHORT || btn == BTN_EVENT_BACK_SHORT) {
        state_machine_transition(&state_ctx, STATE_PAYMENT_METHOD_SELECT);
        redraw = true;
    }
    
    if (redraw) {
        ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
    }
}

// Line 304-315: Replace UP/DOWN with correct button indices
static void handle_state_payment_method_select(button_event_t btn) {
    // ✅ FIX: Use Button-1 and Button-2 (assuming UP=1, DOWN=2)
    if (btn == BTN_EVENT_UP_SHORT) {  // Button-1 → UPI
        state_machine_transition(&state_ctx, STATE_PAYMENT_QR_UPI);
        ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
        network_send_payment_method(TABLE_ID, "upi");
    } else if (btn == BTN_EVENT_DOWN_SHORT) {  // Button-2 → Cash
        state_machine_transition(&state_ctx, STATE_PAYMENT_CASH);
        ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
        network_send_payment_method(TABLE_ID, "cash");
    }
}


static void handle_state_payment_qr_upi(button_event_t btn) {
    // Wait for server to verify payment
}

static void handle_state_payment_cash(button_event_t btn) {
    // Wait for server to verify payment
}

static void handle_state_thank_you(button_event_t btn) {
    // Auto-transitions after timeout
}

// ============================================
// SERVER POLLING
// ============================================

static void poll_server_status(void) {
    // ✅ ADD: Log when polling starts
    ESP_LOGI(TAG, "Polling server for state: %d", state_ctx.current);
    
    if (state_ctx.current != STATE_WAITING_ORDER_ACCEPT &&
        state_ctx.current != STATE_COOKING &&
        state_ctx.current != STATE_WAITING_BILL &&
        state_ctx.current != STATE_PAYMENT_QR_UPI &&
        state_ctx.current != STATE_PAYMENT_CASH) {
        ESP_LOGD(TAG, "Not polling - state doesn't require it");
        return;
    }
    
    char response[1024];
    if (!network_get_table_status(TABLE_ID, response, sizeof(response))) {
        ESP_LOGW(TAG, "Failed to get table status");
        return;
    }
    
    ESP_LOGI(TAG, "Status response: %s", response);
    
    cJSON *root = cJSON_Parse(response);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse status JSON");
        return;
    }
    
    cJSON *status = cJSON_GetObjectItem(root, "status");
    cJSON *order_state = cJSON_GetObjectItem(root, "order_state");
    cJSON *bill_data = cJSON_GetObjectItem(root, "bill_data");
    
    // Handle order state changes
    if (state_ctx.current == STATE_WAITING_ORDER_ACCEPT && order_state) {
        const char *order_state_str = order_state->valuestring;
        ESP_LOGI(TAG, "Order state: %s", order_state_str);
        
        if (strcmp(order_state_str, "accepted") == 0) {
            ESP_LOGI(TAG, "Order ACCEPTED - transitioning to COOKING");
            // Merge pending cart into accepted cart
            cart_merge(&accepted_cart, &pending_cart);
            cart_init(&pending_cart);  // Clear pending cart
            state_machine_transition(&state_ctx, STATE_COOKING);
            state_ctx.append_mode = false;
            ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
        } else if (strcmp(order_state_str, "declined") == 0) {
            ESP_LOGI(TAG, "Order DECLINED");
            // Discard pending cart only, keep accepted cart
            cart_init(&pending_cart);
            state_machine_transition(&state_ctx, STATE_ORDER_DECLINED);
            ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
        }
    }
    
    // Handle food prepared
    if (state_ctx.current == STATE_COOKING && order_state) {
        const char *order_state_str = order_state->valuestring;
        if (strcmp(order_state_str, "prepared") == 0) {
            ESP_LOGI(TAG, "Food PREPARED");
            state_machine_transition(&state_ctx, STATE_FOOD_PREPARED);
            ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
        }
    }
    
    // Handle bill generation
    if (state_ctx.current == STATE_WAITING_BILL && bill_data && bill_data->valuestring) {
        ESP_LOGI(TAG, "Bill received");
        strncpy(bill_json_buffer, bill_data->valuestring, sizeof(bill_json_buffer) - 1);
        state_machine_transition(&state_ctx, STATE_BILL_DISPLAY);
        state_ctx.bill_scroll_index = 0;
        ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
    }
    
    // Handle payment verification
    if ((state_ctx.current == STATE_PAYMENT_QR_UPI ||
         state_ctx.current == STATE_PAYMENT_CASH) && status) {
        const char *status_str = status->valuestring;
        if (strcmp(status_str, "idle") == 0) {
            ESP_LOGI(TAG, "Payment verified");
            state_machine_transition(&state_ctx, STATE_THANK_YOU);
            ui_render_current_state(&state_ctx, &accepted_cart, bill_json_buffer);
        }
    }
    
    cJSON_Delete(root);
}

