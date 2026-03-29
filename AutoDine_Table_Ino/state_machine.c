/* =====================================================================
 *  state_machine.c — AutoDine V4.0
 *
 *  CHANGES vs previous version:
 *   [BUG 2] Global g_order_id moved here as canonical source; accessible
 *           via sm_get_order_id() / sm_set_order_id() so ui_screens.c
 *           always uses the correct order even in append-mode.
 *   [STATE] sm_set() now handles STATE_FOOD_SERVED (defined in header).
 * ===================================================================== */
#include "state_machine.h"
#include "ui_screens.h"
#include "hardware_compat.h"
#include "autodine_net.h"

/* millis() is a C++ Arduino runtime function.
 * Declare it extern here so this .c translation unit can call it
 * without including Arduino.h (which is C++-only).
 * The linker resolves it from the Arduino core object. */
#ifdef ARDUINO
extern unsigned long millis(void);
#endif

static app_state_t s_state    = STATE_SPLASH;
static int         s_order_id = -1;   /* BUG 2: canonical order id */

/* Bug 10: WiFi re-check ticker */
static unsigned long s_wifi_check_ms = 0;
#define WIFI_CHECK_INTERVAL_MS  5000

void sm_init(void)
{
    s_state          = STATE_SPLASH;
    s_order_id       = -1;
    s_wifi_check_ms  = 0;
}

app_state_t sm_get(void)    { return s_state; }
int  sm_get_order_id(void)  { return s_order_id; }
void sm_set_order_id(int id){ s_order_id = id; }

void sm_set(app_state_t new_state)
{
    if (new_state == s_state) return;
    s_state = new_state;

    lvgl_acquire();
    ui_show_screen(new_state);
    lvgl_release();
}

extern int g_pending_buzz;   /* defined in ui_screens.c */

void sm_update(void) {
    /* All deferred logic moved to ui_check_deferred() for safe network execution */
}
