#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    STATE_IDLE = 0,
    STATE_MENU_BROWSE,
    STATE_QUANTITY_SELECT,
    STATE_WAITING_ORDER_ACCEPT,
    STATE_ORDER_DECLINED,
    STATE_ORDER_DECLINED_APPEND,
    STATE_COOKING,
    STATE_FOOD_PREPARED,
    STATE_WAITING_BILL,
    STATE_BILL_DISPLAY,
    STATE_PAYMENT_METHOD_SELECT,
    STATE_PAYMENT_QR_UPI,
    STATE_PAYMENT_CASH,
    STATE_THANK_YOU
} table_state_t;

typedef struct {
    table_state_t current;
    table_state_t previous;
    uint8_t menu_index;
    uint8_t quantity;
    uint8_t bill_scroll_index;
    bool append_mode;
    uint32_t state_entry_time;
} state_context_t;

void state_machine_init(state_context_t *ctx);
void state_machine_transition(state_context_t *ctx, table_state_t new_state);
table_state_t state_machine_get_current(state_context_t *ctx);
uint32_t state_machine_time_in_state(state_context_t *ctx);

#endif
