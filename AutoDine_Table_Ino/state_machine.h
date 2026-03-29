#pragma once
/* =====================================================================
 *  state_machine.h — AutoDine V4.0
 *
 *  CHANGES vs previous version:
 *   [BUG 1] Added STATE_FOOD_SERVED between STATE_FOOD_READY and STATE_BILL.
 *           STATE_COUNT incremented accordingly.
 *   [BUG 2] Added sm_get_order_id() / sm_set_order_id() so append-mode
 *           always passes the correct order_id through the full flow.
 * ===================================================================== */
#include <stdint.h>

typedef enum {
    STATE_SPLASH = 0,
    STATE_MENU,
    STATE_ORDER_PLACED,
    STATE_FOOD_READY,
    STATE_FOOD_SERVED,   /* BUG 1: new dedicated "food served" celebration screen */
    STATE_BILL,
    STATE_PAYMENT_SELECT,
    STATE_PAYMENT_UPI,
    STATE_PAYMENT_CASH,
    STATE_FEEDBACK,
    STATE_COUNT
} app_state_t;

void        sm_init(void);
app_state_t sm_get(void);
void        sm_set(app_state_t new_state);

/* BUG 2: order-id accessors so every module reads the canonical value */
int         sm_get_order_id(void);
void        sm_set_order_id(int oid);

/* Called every LVGL tick cycle from the LVGL task */
void        sm_update(void);
