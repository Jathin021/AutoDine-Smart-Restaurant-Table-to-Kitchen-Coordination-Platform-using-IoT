#include "state_machine.h"
#include "esp_timer.h"

void state_machine_init(state_context_t *ctx) {
    ctx->current = STATE_IDLE;
    ctx->previous = STATE_IDLE;
    ctx->menu_index = 0;
    ctx->quantity = 1;
    ctx->bill_scroll_index = 0;
    ctx->append_mode = false;
    ctx->state_entry_time = (uint32_t)(esp_timer_get_time() / 1000);
}

void state_machine_transition(state_context_t *ctx, table_state_t new_state) {
    ctx->previous = ctx->current;
    ctx->current = new_state;
    ctx->state_entry_time = (uint32_t)(esp_timer_get_time() / 1000);
}

table_state_t state_machine_get_current(state_context_t *ctx) {
    return ctx->current;
}

uint32_t state_machine_time_in_state(state_context_t *ctx) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return (now - ctx->state_entry_time);
}
