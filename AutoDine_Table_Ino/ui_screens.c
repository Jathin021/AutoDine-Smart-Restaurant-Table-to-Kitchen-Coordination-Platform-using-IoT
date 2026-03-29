/* =====================================================================
 *  ui_screens.c — AutoDine V4.0
 *  Main UI logic and screen definitions.
 * ===================================================================== */
#include "ui_screens.h"
#include "state_machine.h"
#include "cart.h"
#include "autodine_net.h"
#include "app_config.h"
#include "hardware_compat.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- THEME / COLORS (WOW Aesthetics) ----------------------- */
#define COL_BG       lv_color_hex(0x020617) /* Deep Navy           */
#define COL_BG2      lv_color_hex(0x0F172A) /* Slate Gradient      */
#define COL_CARD     lv_color_hex(0x0F172A) /* Slate Navy          */
#define COL_CARD2    lv_color_hex(0x1E293B) /* Lighter Slate       */
#define COL_AMBER    lv_color_hex(0xF59E0B) /* Premium Gold        */
#define COL_AMBER2   lv_color_hex(0xD97706) /* Dimmed Gold         */
#define COL_EMERALD  lv_color_hex(0x10B981) /* Success Emerald     */
#define COL_SUCCESS  lv_color_hex(0x10B981) /* (Alias)             */
#define COL_WHITE    lv_color_hex(0xF8FAFC) /* Off White           */
#define COL_GREY     lv_color_hex(0x64748B) /* Slate Grey          */
#define COL_ERROR    lv_color_hex(0xEF4444) /* Error Red           */
#define COL_INFO     lv_color_hex(0x3B82F6) /* Info Blue           */
#define COL_DARK     lv_color_hex(0x020617) /* Pure Dark           */

/* Map missing/non-standard LVGL symbols to ASCII/text fallbacks */
#define LV_SYMBOL_STAR   "*"
/* These two are NOT in standard LVGL — replace with text icons */
#define LV_SYMBOL_QR     "[QR]"
#define LV_SYMBOL_CASH   "Rs."

/* ---- Global handles ---------------------------------------- */
/* BUG 2: g_order_id managed via sm_get_order_id()/sm_set_order_id() */
static bool  g_wifi_ok      = false;
int          g_pending_buzz = 0;

/* ---- Append-mode / Razorpay / Feedback state --------------- */
static bool  g_append_mode  = false; 
static bool  g_goto_menu    = false; 
static bool  g_goto_splash  = false;
static bool  g_pending_upi_fetch = false;
static bool  g_pending_upi_poll  = false;
static char  g_razorpay_url[512] = ""; 
static char  g_last_status[32]  = {0};  
static lv_obj_t *lbl_waiter_status = NULL; 
static int   g_stars        = 0;     /* manual 1-5 feedback stars       */
static int   g_star_rating  = 0;     /* 1-5 feedback stars              */
static int   fb_seconds     = 20;    /* feedback countdown seconds      */
static int   upi_timeout_count = 0;  /* 3s polls, 100 = 5 minutes       */
static bool  g_pending_food_served = false; /* deferred serving update */
static int   g_total_bill_rupees   = 0;     /* global store for payment screens */

static lv_timer_t *poll_timer      = NULL; 
static lv_timer_t *upi_poll_timer  = NULL; 
static lv_timer_t *cash_poll_timer = NULL; 
static lv_timer_t *feedback_timer  = NULL; 

/* Timer callback to auto-delete an object (e.g., a toast) after a delay */
static void auto_del_obj_timer_cb(lv_timer_t *t)
{
    if (t->user_data) lv_obj_del((lv_obj_t *)t->user_data);
    lv_timer_del(t);
}

/* LVGL Animation Wrappers (Needed because style functions take 3 args, but anims only pass 2) */
static void anim_zoom_cb(void *var, int32_t v) {
    lv_obj_set_style_transform_zoom((lv_obj_t *)var, v, 0);
}
static void anim_translate_y_cb(void *var, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}
static void anim_opa_cb(void *var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
}

/* NEW: Generic Toast with auto-delete */
static void create_toast(const char *title, const char *msg, uint32_t delay)
{
    lv_obj_t *toast = lv_obj_create(lv_scr_act());
    lv_obj_set_size(toast, 640, 68);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0xEF4444), 0); /* Red */
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_shadow_width(toast, 20, 0);
    lv_obj_set_style_shadow_opa(toast, 80, 0);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tl = lv_label_create(toast);
    lv_label_set_text_fmt(tl, "#FFFFFF %s#", title);
    lv_label_set_recolor(tl, true);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, -2);

    lv_obj_t *ml = lv_label_create(toast);
    lv_label_set_text(ml, msg);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_12, 0);
    lv_obj_align(ml, LV_ALIGN_BOTTOM_MID, 0, 2);

    lv_timer_t *tt = lv_timer_create(auto_del_obj_timer_cb, delay, (void*)toast);
    if (tt) lv_timer_set_repeat_count(tt, 1);
}

static void safe_timer_del(lv_timer_t **t)
{
    if (t && *t) {
        lv_timer_del(*t);
        *t = NULL;
    }
}

static lv_obj_t *lbl_append_banner = NULL; /* orange append-mode banner */
static lv_obj_t *lbl_cart_title    = NULL; /* "YOUR ORDER" / "NEW ITEMS" */
static lv_obj_t *btn_place_order   = NULL; /* reference so we can re-label it */

/* Screen containers */
static lv_obj_t *scr_splash       = NULL;
static lv_obj_t *scr_menu         = NULL;
static lv_obj_t *scr_placed       = NULL;
static lv_obj_t *scr_ready        = NULL;
static lv_obj_t *scr_food_served  = NULL;
static lv_obj_t *scr_bill         = NULL;
static lv_obj_t *scr_paysel       = NULL;
static lv_obj_t *scr_upi          = NULL;
static lv_obj_t *scr_cash         = NULL;
static lv_obj_t *scr_feedback     = NULL;

/* Persistent widget references */
static lv_obj_t *lbl_wifi_status  = NULL;
static lv_obj_t *cart_list        = NULL;
static lv_obj_t *lbl_cart_total   = NULL;
static lv_obj_t *lbl_cart_count   = NULL;
static lv_obj_t *menu_grid        = NULL;
static lv_obj_t *lbl_bill_body    = NULL;
static lv_obj_t *lbl_paysel_amount = NULL;
static lv_obj_t *lbl_upi_amount   = NULL;
static lv_obj_t *lbl_cash_amount  = NULL;
static lv_obj_t *lbl_fb_countdown = NULL;
static lv_obj_t *lbl_payment_result = NULL;
static lv_obj_t *lbl_order_id_placed = NULL;
static bool g_pending_cash_select = false;
static bool g_pending_wifi_check = false;
static uint32_t last_wifi_ms      = 0;
static lv_obj_t *feedback_ta      = NULL;
static lv_obj_t *star_btns[5]     = {NULL};
static lv_obj_t *lbl_star_rating  = NULL;

/* Bill screen dynamic widget handles (populated by load_bill_cb) */
static lv_obj_t *bill_items_col = NULL;
static lv_obj_t *lbl_bill_sub   = NULL;
static lv_obj_t *lbl_bill_gst   = NULL;
static lv_obj_t *lbl_bill_time  = NULL;

/* Animatable widget handles for UPI / Cash screens */
static lv_obj_t *upi_qr_box    = NULL;
static lv_obj_t *upi_qr_obj    = NULL; /* LVGL native QR code widget  */
static lv_obj_t *upi_spinner   = NULL; /* loading spinner             */
static lv_obj_t *upi_info_card = NULL;
static lv_obj_t *cash_circle   = NULL;
static lv_obj_t *cash_amt_lbl  = NULL;
static lv_obj_t *upi_qr_label  = NULL; /* fallback text label         */

/* Feedback submit button — repositioned when keyboard opens */
static lv_obj_t *fb_submit_btn = NULL;

/* =====================================================================
 *  Helpers
 * ===================================================================== */

static const char *find_obj_end(const char *start)
{
    int depth = 0;
    for (const char *c = start; *c; c++) {
        if (*c == '"') {
            c++;
            while (*c && *c != '"') { if (*c == '\\') c++; c++; }
            if (!*c) return NULL;
        } else if (*c == '{') {
            depth++;
        } else if (*c == '}') {
            depth--;
            if (depth == 0) return c;
        }
    }
    return NULL;
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_set_style_bg_grad_color(s, COL_BG2, 0);
    lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static lv_obj_t *make_card(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, 235, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(c, 24, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_set_style_shadow_width(c, 30, 0);
    lv_obj_set_style_shadow_color(c, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(c, 100, 0);
    return c;
}

/* Amber top-accent stripe — call after card is positioned */
static void add_card_accent(lv_obj_t *card, int card_w)
{
    lv_obj_t *acc = lv_obj_create(card);
    lv_obj_set_size(acc, card_w - 28, 3);
    lv_obj_set_pos(acc, 0, -14);
    lv_obj_set_style_bg_color(acc, COL_AMBER, 0);
    lv_obj_set_style_bg_opa(acc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(acc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(acc, 2, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt,
                             lv_color_t col, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, col, 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static lv_obj_t *make_amber_btn(lv_obj_t *parent, const char *txt,
                                 int w, int h)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, COL_AMBER, 0);
    lv_obj_set_style_bg_grad_color(btn, COL_AMBER2, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_color(btn, COL_AMBER2, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn, 10, 0);
    lv_obj_set_style_shadow_color(btn, COL_AMBER, 0);
    lv_obj_set_style_shadow_opa(btn, 60, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    return btn;
}

static void refresh_cart_panel(void)
{
    if (!cart_list || !lbl_cart_total) return;
    lv_obj_clean(cart_list);
    const cart_item_t *items = cart_get_items();
    int count = cart_item_count();
    for (int i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(cart_list);
        lv_obj_set_size(row, 186, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, (i % 2 == 0) ? COL_CARD : COL_CARD2, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 4, 0);

        char line[96];
        snprintf(line, sizeof(line), "%s x%d  Rs. %d",
                 items[i].name, items[i].qty,
                 items[i].price_paise * items[i].qty / 100);
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, line);
        lv_obj_set_style_text_color(lbl, COL_WHITE, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, 178);
    }
    char total[48];
    snprintf(total, sizeof(total), "Total: Rs. %d",
             cart_grand_total_paise() / 100);
    lv_label_set_text(lbl_cart_total, total);

    /* Update topbar cart count badge */
    if (lbl_cart_count) {
        char badge[24];
        snprintf(badge, sizeof(badge), "Cart: %d", cart_total_items());
        lv_label_set_text(lbl_cart_count, badge);
    }
}

/* =====================================================================
 *  SCREEN 1 — SPLASH
 * ===================================================================== */
static void splash_tap_cb(lv_event_t *e) { sm_set(STATE_MENU); }

static void build_splash(void)
{
    scr_splash = make_screen();

    /* Top-left WiFi row */
    lv_obj_t *wifi_row = lv_obj_create(scr_splash);
    lv_obj_set_size(wifi_row, 220, 30);
    lv_obj_set_pos(wifi_row, 10, 10);
    lv_obj_set_style_bg_opa(wifi_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(wifi_row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(wifi_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wifi_row, 6, 0);

    lv_obj_t *wifi_icon = make_label(wifi_row, LV_SYMBOL_WIFI,
                                     COL_ERROR, &lv_font_montserrat_14);
    lbl_wifi_status = make_label(wifi_row, "Connecting...",
                                 COL_ERROR, &lv_font_montserrat_12);
    (void)wifi_icon;

    /* Top-right table label */
    lv_obj_t *lbl_tbl = make_label(scr_splash, TABLE_LABEL_UC,
                                   COL_GREY, &lv_font_montserrat_12);
    lv_obj_align(lbl_tbl, LV_ALIGN_TOP_RIGHT, -16, 16);

    /* Hero card */
    lv_obj_t *hero = make_card(scr_splash, 600, 260);
    lv_obj_align(hero, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_grad_color(hero, COL_DARK, 0);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(hero, COL_AMBER, 0);
    lv_obj_set_style_border_width(hero, 2, 0);
    lv_obj_set_style_border_opa(hero, LV_OPA_COVER, 0);
    add_card_accent(hero, 600);

    lv_obj_t *lbl_brand = make_label(hero, "AUTODINE",
                                     COL_AMBER, &lv_font_montserrat_40);
    lv_obj_align(lbl_brand, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *lbl_ver = make_label(hero, "V4.0",
                                   COL_AMBER2, &lv_font_montserrat_20);
    lv_obj_align(lbl_ver, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *lbl_sub = make_label(hero, "Smart Restaurant Experience",
                                   COL_GREY, &lv_font_montserrat_16);
    lv_obj_align(lbl_sub, LV_ALIGN_CENTER, 0, 50);

    /* Table badge pill at bottom-right of hero */
    lv_obj_t *tbl_pill = lv_obj_create(hero);
    lv_obj_set_size(tbl_pill, 80, 26);
    lv_obj_align(tbl_pill, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_opa(tbl_pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(tbl_pill, COL_AMBER, 0);
    lv_obj_set_style_border_width(tbl_pill, 1, 0);
    lv_obj_set_style_border_opa(tbl_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tbl_pill, 13, 0);
    lv_obj_t *lbl_pill = make_label(tbl_pill, TABLE_LABEL, COL_AMBER,
                                    &lv_font_montserrat_12);
    lv_obj_center(lbl_pill);

    /* Touch to start button */
    lv_obj_t *btn = make_amber_btn(scr_splash, "TOUCH TO START", 580, 60);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_add_event_cb(btn, splash_tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr_splash, splash_tap_cb, LV_EVENT_CLICKED, NULL);
}

/* =====================================================================
 *  SCREEN 2 — MENU
 * ===================================================================== */
typedef struct {
    int  id;
    int  price_paise;
    bool is_veg;
    char name[CART_MAX_NAME];
} menu_item_ctx_t;

static void qty_plus_cb(lv_event_t *e)
{
    menu_item_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) return;
    cart_add(ctx->id, ctx->name, ctx->price_paise, ctx->is_veg);
    refresh_cart_panel();
}

static void qty_minus_cb(lv_event_t *e)
{
    menu_item_ctx_t *ctx = lv_event_get_user_data(e);
    if (!ctx) return;
    cart_remove_one(ctx->id);
    refresh_cart_panel();
}

static void place_order_cb(lv_event_t *e)
{
    static bool placing = false;
    if (placing || cart_item_count() == 0) return;
    placing = true;

    char *json = cart_to_json();
    if (!json) { placing = false; return; }

    if (g_append_mode) {
        /* === APPEND MODE: add new items to existing order === */
        int oid = sm_get_order_id();
        /* Extract just the items array from cart_to_json() output.
         * cart_to_json() returns {"table":N,"items":[...]}.
         * We need the [...] array string for net_append_order(). */
        const char *items_start = strstr(json, "\"items\":");
        if (items_start) {
            items_start += 8; /* skip "items": */
        } else {
            items_start = "[]"; /* fallback */
        }
        /* items_start now points at the [ of the items array */
        /* Find end of items array by brace matching */
        int depth = 0;
        const char *end = items_start;
        bool in_str = false;
        while (*end) {
            if (!in_str) {
                if (*end == '[' || *end == '{') depth++;
                else if (*end == ']' || *end == '}') { depth--; if (depth==0){end++;break;} }
                else if (*end == '"') in_str = true;
            } else {
                if (*end == '\\') { end++; } /* skip escape */
                else if (*end == '"') in_str = false;
            }
            end++;
        }
        /* Copy just the [...] portion */
        int arr_len = (int)(end - items_start);
        char *arr = (char*)malloc(arr_len + 1);
        if (arr) {
            memcpy(arr, items_start, arr_len);
            arr[arr_len] = '\0';
        }
        free(json);
        placing = false;
        if (!arr) return;
        int err = net_append_order(oid, arr);
        free(arr);
        if (err == 0) {
            g_append_mode = false;
            cart_clear();
            sm_set(STATE_ORDER_PLACED);
            /* poll_timer is restarted by ui_show_screen(STATE_ORDER_PLACED) */
        } else {
            /* Show red toast on failure */
            lv_obj_t *toast = lv_obj_create(lv_scr_act());
            lv_obj_set_size(toast, 640, 56);
            lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -16);
            lv_obj_set_style_bg_color(toast, lv_color_hex(0xEF4444), 0);
            lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(toast, 10, 0);
            lv_obj_set_style_border_opa(toast, LV_OPA_TRANSP, 0);
            lv_obj_t *tl = lv_label_create(toast);
            lv_label_set_text(tl, "Failed to add items. Try again.");
            lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
            lv_obj_center(tl);
            /* Auto-delete toast after 2.5s */
            lv_timer_t *tt = lv_timer_create(auto_del_obj_timer_cb, 2500, (void*)toast);
            if (tt) lv_timer_set_repeat_count(tt, 1);
        }
    } else {
        /* === NORMAL MODE: create new order === */
        int oid = -1;
        int err = net_place_order(json, &oid);
        free(json);
        placing = false;  /* CRITICAL: always reset, even on failure */
        if (err == 0 && oid > 0) {
            sm_set_order_id(oid);   /* BUG 2: canonical storage */
            cart_clear(); /* clear cart so Add More starts fresh */
            net_buzz(1);
            sm_set(STATE_ORDER_PLACED);
        } else {
            /* Show error feedback so user knows something went wrong */
            lv_obj_t *toast = lv_obj_create(lv_scr_act());
            lv_obj_set_size(toast, 640, 56);
            lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -16);
            lv_obj_set_style_bg_color(toast, lv_color_hex(0xEF4444), 0);
            lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(toast, 10, 0);
            lv_obj_set_style_border_opa(toast, LV_OPA_TRANSP, 0);
            lv_obj_t *tl = lv_label_create(toast);
            lv_label_set_text(tl, "Order failed. Check WiFi and try again.");
            lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
            lv_obj_center(tl);
            lv_timer_t *tt = lv_timer_create(auto_del_obj_timer_cb, 3000, (void*)toast);
            if (tt) lv_timer_set_repeat_count(tt, 1);
        }
    }
}

static void clear_cart_cb(lv_event_t *e)
{
    cart_clear();
    refresh_cart_panel();
}

/* Bug 8 Fix: waiter call uses pattern 1 (not 2) */
static void call_waiter_menu_cb(lv_event_t *e)
{
    g_pending_buzz = 1;
}

static void build_menu(void)
{
    scr_menu = make_screen();

    /* Top bar — 56px */
    lv_obj_t *topbar = lv_obj_create(scr_menu);
    lv_obj_set_size(topbar, 800, 56);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_style_bg_color(topbar, COL_CARD, 0);
    lv_obj_set_style_border_opa(topbar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(topbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(topbar, COL_AMBER, 0);
    lv_obj_set_style_border_width(topbar, 2, 0);
    lv_obj_set_style_border_opa(topbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 0, 0);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_logo = make_label(topbar, "AutoDine Kitchen",
                                    COL_AMBER, &lv_font_montserrat_16);
    lv_obj_align_to(lbl_logo, topbar, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *lbl_t = make_label(topbar, TABLE_LABEL,
                                 COL_WHITE, &lv_font_montserrat_14);
    lv_obj_align_to(lbl_t, topbar, LV_ALIGN_CENTER, 0, 0);

    lbl_cart_count = make_label(topbar, "Cart: 0",
                                COL_GREY, &lv_font_montserrat_12);
    lv_obj_align_to(lbl_cart_count, topbar, LV_ALIGN_RIGHT_MID, -14, 0);
    /* BUG 1 FIX: Append-mode orange banner (hidden by default, shown when g_append_mode) */
    lbl_append_banner = lv_obj_create(scr_menu);
    lv_obj_set_size(lbl_append_banner, 800, 28);
    lv_obj_set_pos(lbl_append_banner, 0, 56);
    lv_obj_set_style_bg_color(lbl_append_banner, lv_color_hex(0xFF6B35), 0);
    lv_obj_set_style_bg_opa(lbl_append_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(lbl_append_banner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(lbl_append_banner, 0, 0);
    lv_obj_set_style_pad_all(lbl_append_banner, 0, 0);
    lv_obj_clear_flag(lbl_append_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl_banner_text = lv_label_create(lbl_append_banner);
    lv_label_set_text(lbl_banner_text, "Adding to Order #0"); /* updated dynamically */
    lv_obj_set_style_text_color(lbl_banner_text, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_banner_text, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_banner_text);
    lv_obj_add_flag(lbl_append_banner, LV_OBJ_FLAG_HIDDEN); /* hidden until append mode */

    /* Right panel — cart (210×424) */
    lv_obj_t *cart_panel = make_card(scr_menu, 210, 424);
    lv_obj_set_pos(cart_panel, 582, 56);
    lv_obj_set_style_pad_all(cart_panel, 10, 0);
    lv_obj_set_flex_flow(cart_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cart_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(cart_panel, 6, 0);
    add_card_accent(cart_panel, 210);

    lbl_cart_title = make_label(cart_panel, "YOUR ORDER", COL_AMBER, &lv_font_montserrat_14);

    cart_list = lv_obj_create(cart_panel);
    lv_obj_set_size(cart_list, 188, 200);
    lv_obj_set_style_bg_opa(cart_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(cart_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(cart_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cart_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(cart_list, 2, 0);

    lbl_cart_total = make_label(cart_panel, "Total: Rs. 0",
                                COL_AMBER, &lv_font_montserrat_14);

    /* BUG 1 FIX: Place Order button — store reference for label change in append mode */
    btn_place_order = make_amber_btn(cart_panel, "PLACE ORDER", 186, 52);
    lv_obj_set_style_text_font(
        lv_obj_get_child(btn_place_order, 0), &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(btn_place_order, place_order_cb, LV_EVENT_CLICKED, NULL);

    /* Clear cart */
    lv_obj_t *btn_clear = lv_btn_create(cart_panel);
    lv_obj_set_size(btn_clear, 186, 36);
    lv_obj_set_style_bg_color(btn_clear, COL_CARD, 0);
    lv_obj_set_style_border_color(btn_clear, COL_ERROR, 0);
    lv_obj_set_style_border_width(btn_clear, 1, 0);
    lv_obj_set_style_border_opa(btn_clear, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_clear, 8, 0);
    lv_obj_t *lbl_cl = lv_label_create(btn_clear);
    lv_label_set_text(lbl_cl, LV_SYMBOL_TRASH "  Clear Cart");
    lv_obj_set_style_text_color(lbl_cl, COL_ERROR, 0);
    lv_obj_set_style_text_font(lbl_cl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_cl);
    lv_obj_add_event_cb(btn_clear, clear_cart_cb, LV_EVENT_CLICKED, NULL);

    /* Call waiter button — uses LV_SYMBOL_BELL (built-in LVGL glyph) */
    lv_obj_t *btn_waiter = lv_btn_create(cart_panel);
    lv_obj_set_size(btn_waiter, 186, 36);
    lv_obj_set_style_bg_color(btn_waiter, COL_ERROR, 0);
    lv_obj_set_style_radius(btn_waiter, 8, 0);
    lv_obj_set_style_border_opa(btn_waiter, LV_OPA_TRANSP, 0);
    lv_obj_t *lbl_w = lv_label_create(btn_waiter);
    lv_label_set_text(lbl_w, LV_SYMBOL_BELL "  Call Waiter");
    lv_obj_set_style_text_color(lbl_w, COL_WHITE, 0);
    lv_obj_set_style_text_font(lbl_w, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_w);
    lv_obj_add_event_cb(btn_waiter, call_waiter_menu_cb, LV_EVENT_CLICKED, NULL);

    /* Menu grid area */
    menu_grid = lv_obj_create(scr_menu);
    lv_obj_set_size(menu_grid, 578, 424);
    lv_obj_set_pos(menu_grid, 0, 56);
    lv_obj_set_style_bg_opa(menu_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(menu_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(menu_grid, 8, 0);
    lv_obj_set_flex_flow(menu_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(menu_grid, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(menu_grid, 6, 0);
    /* Smooth scrolling — auto-hide scrollbar, enable momentum */
    lv_obj_set_scrollbar_mode(menu_grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(menu_grid, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_snap_y(menu_grid, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_anim_time(menu_grid, 300, 0); /* smooth deceleration */
}

/* Category left-border color */
static lv_color_t cat_color_for(const char *cat)
{
    if (!cat) return COL_AMBER;
    char lower[64];
    strncpy(lower, cat, 63); lower[63] = 0;
    for (char *p = lower; *p; p++) *p = tolower((unsigned char)*p);

    if (strstr(lower, "starter") || strstr(lower, "appetizer")) return COL_SUCCESS;
    if (strstr(lower, "main") || strstr(lower, "course")) return COL_AMBER;
    if (strstr(lower, "drink") || strstr(lower, "beverage")) return lv_color_hex(0x3B82F6); /* blue */
    if (strstr(lower, "dessert") || strstr(lower, "sweet")) return lv_color_hex(0xEC4899); /* pink */
    return lv_color_hex(0x94A3B8); /* grey for others */
}

static void make_category_header(const char *cat_name)
{
    lv_obj_t *hdr = lv_obj_create(menu_grid);
    lv_obj_set_size(hdr, 550, 40);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = make_label(hdr, cat_name, cat_color_for(cat_name), &lv_font_montserrat_22);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *line = lv_obj_create(hdr);
    lv_obj_set_size(line, 550, 1);
    lv_obj_align(line, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_opa(line, LV_OPA_TRANSP, 0);
}

static void free_ctx_cb(lv_event_t *e) {
    void *ctx = lv_event_get_user_data(e);
    if (ctx) free(ctx);
}

/* Add a card to the menu grid */
static void add_menu_card(int id, const char *name, const char *desc, const char *cat,
                           int price_paise, bool is_veg, bool available)
{
    lv_obj_t *card = make_card(menu_grid, 274, 145);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_left(card, 12, 0);

    /* Category left-border (3px) */
    lv_obj_t *cat_bar = lv_obj_create(card);
    lv_obj_set_size(cat_bar, 3, 129);
    lv_obj_set_pos(cat_bar, -12, -8);
    lv_obj_set_style_bg_color(cat_bar, cat_color_for(cat), 0);
    lv_obj_set_style_bg_opa(cat_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(cat_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(cat_bar, 0, 0);

    /* Veg/non-veg dot — 12×12 with white border */
    lv_obj_t *dot = lv_obj_create(card);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, is_veg ? COL_SUCCESS : COL_ERROR, 0);
    lv_obj_set_style_border_color(dot, COL_WHITE, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -2, 2);

    /* Name */
    lv_obj_t *lbl_name = make_label(card, name, COL_WHITE,
                                    &lv_font_montserrat_14);
    lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_name, 200);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Description */
    lv_obj_t *lbl_desc = make_label(card, desc, COL_GREY,
                                    &lv_font_montserrat_10);
    lv_label_set_long_mode(lbl_desc, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_desc, 255);
    lv_obj_align(lbl_desc, LV_ALIGN_TOP_LEFT, 0, 22);

    /* Price — larger, amber */
    char price_str[24];
    snprintf(price_str, sizeof(price_str), "Rs. %d", price_paise / 100);
    lv_obj_t *lbl_price = make_label(card, price_str, COL_AMBER,
                                     &lv_font_montserrat_20);
    lv_obj_align(lbl_price, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Qty controls: [−] [qty] [+] */
    menu_item_ctx_t *ctx = malloc(sizeof(menu_item_ctx_t));
    if (ctx) {
        ctx->id = id;
        ctx->price_paise = price_paise;
        ctx->is_veg = is_veg;
        strncpy(ctx->name, name, CART_MAX_NAME - 1);
        ctx->name[CART_MAX_NAME - 1] = '\0';
    }

    lv_obj_t *btn_p = lv_btn_create(card);
    lv_obj_set_size(btn_p, 28, 28);
    lv_obj_set_style_bg_color(btn_p, COL_AMBER, 0);
    lv_obj_set_style_radius(btn_p, 6, 0);
    lv_obj_set_style_border_opa(btn_p, LV_OPA_TRANSP, 0);
    lv_obj_align(btn_p, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *lp = lv_label_create(btn_p);
    lv_label_set_text(lp, "+");
    lv_obj_set_style_text_color(lp, lv_color_hex(0x0A0A0A), 0);
    lv_obj_center(lp);
    lv_obj_add_event_cb(btn_p, qty_plus_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(btn_p, free_ctx_cb, LV_EVENT_DELETE, ctx); /* free when button is deleted */

    lv_obj_t *btn_m = lv_btn_create(card);
    lv_obj_set_size(btn_m, 28, 28);
    lv_obj_set_style_bg_color(btn_m, COL_AMBER, 0);
    lv_obj_set_style_radius(btn_m, 6, 0);
    lv_obj_set_style_border_opa(btn_m, LV_OPA_TRANSP, 0);
    lv_obj_align(btn_m, LV_ALIGN_BOTTOM_RIGHT, -34, 0);
    lv_obj_t *lm = lv_label_create(btn_m);
    lv_label_set_text(lm, "-");
    lv_obj_set_style_text_color(lm, lv_color_hex(0x0A0A0A), 0);
    lv_obj_center(lm);
    lv_obj_add_event_cb(btn_m, qty_minus_cb, LV_EVENT_CLICKED, ctx);
    /* No need for delete_cb on btn_m because they share the same ctx freed by btn_p */

    /* Unavailable overlay */
    if (!available) {
        lv_obj_t *overlay = lv_obj_create(card);
        lv_obj_set_size(overlay, 280, 145);
        lv_obj_set_pos(overlay, -12, -8);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0A0A0A), 0);
        lv_obj_set_style_bg_opa(overlay, 190, 0);
        lv_obj_set_style_border_opa(overlay, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(overlay, 16, 0);
        lv_obj_t *lbl_na = make_label(overlay, "Currently Unavailable",
                                      COL_GREY, &lv_font_montserrat_12);
        lv_obj_center(lbl_na);
    }
}

/* =====================================================================
 *  SCREEN 3 — ORDER PLACED
 * ===================================================================== */
/* BUG 1 FIX: add_more_cb — DEFERRED to prevent stack overflow from LVGL callbacks */
static void add_more_cb(lv_event_t *e)
{
    g_append_mode = true;
    g_goto_menu   = true;  /* Deferred: handled in ui_task_handler */
}

/* BUG 7: track which removed items we've already toasted */
static int g_notified_removed[32];
static int g_notified_removed_count = 0;

/* Bug 1 Fix: state guard prevents stale timer firing in wrong state */
static void order_poll_cb(lv_timer_t *t)
{
    if (poll_timer == NULL) return;
    int oid = sm_get_order_id();
    if (oid <= 0) return;
    
    static char json[1024]; /* Move to static to save stack space */
    if (net_get_order_json(oid, json, sizeof(json)) == 0) {
        /* 1. Extract status */
        char status[32] = "";
        const char *ps = strstr(json, "\"status\"");
        if (ps) {
            ps = strchr(ps, ':');
            if (ps) {
                ps++; while(*ps == ' ' || *ps == '"') ps++;
                const char *e = strchr(ps, '"');
                if (e) {
                    int l = e - ps;
                    if (l > 31) l = 31;
                    memcpy(status, ps, l);
                    status[l] = '\0';
                }
            }
        }
        
        /* 2. Check for Transitions */
        if (strcmp(status, "ready") == 0) {
            safe_timer_del(&poll_timer);
            sm_set(STATE_FOOD_READY);
        } else if (strcmp(status, "served") == 0) {
            safe_timer_del(&poll_timer);
            sm_set(STATE_FOOD_SERVED);
        }
    }
}

static void bell_anim_cb(void * var, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void build_order_placed(void)
{
    scr_placed = make_screen();
    lv_obj_t *card = make_card(scr_placed, 560, 340);
    lv_obj_center(card);
    add_card_accent(card, 560);

    lv_obj_t *circle = lv_obj_create(card);
    lv_obj_set_size(circle, 80, 80);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, COL_AMBER, 0);
    lv_obj_set_style_border_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_t *lbl_ok = make_label(circle, LV_SYMBOL_OK,
                                  lv_color_hex(0x0A0A0A), &lv_font_montserrat_28);
    lv_obj_center(lbl_ok);

    lv_obj_t *lbl_title = make_label(card, "Order Placed!",
                                     COL_AMBER, &lv_font_montserrat_32);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 112);

    lbl_order_id_placed = make_label(card, "Sending to kitchen...",
                                     COL_WHITE, &lv_font_montserrat_14);
    lv_obj_align(lbl_order_id_placed, LV_ALIGN_TOP_MID, 0, 158);

    lv_obj_t *bar = lv_bar_create(card);
    lv_obj_set_size(bar, 440, 8);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_bg_color(bar, COL_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 30, LV_ANIM_OFF);

    lv_obj_t *lbl_wait = make_label(card, "~15 min estimated",
                                    COL_GREY, &lv_font_montserrat_14);
    lv_obj_align(lbl_wait, LV_ALIGN_TOP_MID, 0, 208);

    lv_obj_t *btn_more = make_amber_btn(card, "Add More Items", 220, 52);
    lv_obj_align(btn_more, LV_ALIGN_BOTTOM_LEFT, 10, -12);
    lv_obj_add_event_cb(btn_more, add_more_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_waiter = lv_btn_create(card);
    lv_obj_set_size(btn_waiter, 190, 52);
    lv_obj_set_style_bg_color(btn_waiter, COL_ERROR, 0);
    lv_obj_set_style_radius(btn_waiter, 12, 0);
    lv_obj_set_style_border_opa(btn_waiter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_color(btn_waiter, COL_ERROR, 0);
    lv_obj_set_style_shadow_opa(btn_waiter, 120, 0);
    lv_obj_set_style_shadow_width(btn_waiter, 12, 0);
    lv_obj_align(btn_waiter, LV_ALIGN_BOTTOM_RIGHT, -10, -12);
    lv_obj_t *lbl_w2 = lv_label_create(btn_waiter);
    lv_label_set_text(lbl_w2, LV_SYMBOL_BELL " Call Waiter");
    lv_obj_set_style_text_color(lbl_w2, COL_WHITE, 0);
    lv_obj_center(lbl_w2);
    lv_obj_add_event_cb(btn_waiter, call_waiter_menu_cb, LV_EVENT_CLICKED, NULL);

    /* --- Bounce Animation Array On Bell Button --- */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, btn_waiter);
    lv_anim_set_values(&a, 0, -8);
    lv_anim_set_time(&a, 400);
    lv_anim_set_playback_time(&a, 400);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, bell_anim_cb);
    lv_anim_start(&a);
}

/* =====================================================================
 *  SCREEN 4 — FOOD READY
 * ===================================================================== */
/* BUG 1: go to dedicated food-served screen, not directly to bill */
static void food_served_cb(lv_event_t *e) {
    g_pending_food_served = true; /* Fix: immediate flag for 1-click served */
    sm_set(STATE_FOOD_SERVED);
}
static void call_waiter_cb(lv_event_t *e) {
    net_call_waiter(sm_get_order_id());
    create_toast("STAFF NOTIFIED", "Someone is coming to your table.", 3000);
}
static void gen_bill_cb(lv_event_t *e) { sm_set(STATE_BILL); }

static void build_food_ready(void)
{
    scr_ready = make_screen();
    lv_obj_t *card = make_card(scr_ready, 640, 360);
    lv_obj_center(card);
    add_card_accent(card, 640);

    /* Attractive Icon */
    lv_obj_t *icon = make_label(card, "\xf0\x9f\x8d\xb3", COL_SUCCESS, &lv_font_montserrat_48);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *lbl_rdy = make_label(card, "Chef has Prepared Your Feast!",
                                   COL_SUCCESS, &lv_font_montserrat_28);
    lv_obj_align(lbl_rdy, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *lbl_enjoy = make_label(card, "Your delicious order is ready for pickup.",
                                     COL_GREY, &lv_font_montserrat_16);
    lv_obj_align(lbl_enjoy, LV_ALIGN_TOP_MID, 0, 115);

    /* Centered Large "Food Received" Button */
    lv_obj_t *btn_rcvd = make_amber_btn(card, LV_SYMBOL_OK "  I have Received the Food", 440, 80);
    lv_obj_align(btn_rcvd, LV_ALIGN_CENTER, 0, 45);
    lv_obj_set_style_bg_color(btn_rcvd, COL_SUCCESS, 0);
    lv_obj_set_style_radius(btn_rcvd, 20, 0);
    lv_obj_add_event_cb(btn_rcvd, food_served_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_waiter = make_amber_btn(card, LV_SYMBOL_BELL "  Need Help?", 200, 44);
    lv_obj_align(btn_waiter, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn_waiter, COL_CARD2, 0);
    lv_obj_set_style_border_color(btn_waiter, COL_GREY, 0);
    lv_obj_set_style_border_width(btn_waiter, 1, 0);
    lv_obj_add_event_cb(btn_waiter, call_waiter_cb, LV_EVENT_CLICKED, NULL);
}

/* =====================================================================
 *  SCREEN 4b — FOOD SERVED  (BUG 1: dedicated celebration + 3 options)
 * ===================================================================== */
typedef struct { lv_obj_t *btn; } stagger_ctx_t;

static void fs_btn_show_cb(lv_timer_t *t)
{
    stagger_ctx_t *ctx = (stagger_ctx_t *)t->user_data;
    lv_timer_del(t);
    if (!ctx) return;
    lv_obj_clear_flag(ctx->btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_fade_in(ctx->btn, 300, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ctx->btn);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    int orig_y = lv_obj_get_y(ctx->btn);
    lv_anim_set_values(&a, orig_y + 40, orig_y);
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    free(ctx);
}

/* BUG 2: fs_order_more_cb routes through add_more_cb so append-mode is set */
static void fs_order_more_cb(lv_event_t *e)  { add_more_cb(e); }
static void fs_call_waiter_cb(lv_event_t *e) { g_pending_buzz = 1; }

/* BUG 2 FIX: fs_gen_bill_cb — only called from "Generate Bill" button.
 * Calls net_food_served(); retries once on failure, then shows waiter toast. */
static lv_obj_t *g_fs_retry_btn_ref = NULL;
static void fs_retry_served_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    int err = net_food_served(sm_get_order_id());
    if (err == 0) {
        net_buzz(4);
        sm_set(STATE_BILL);
    } else {
        /* Final failure — show persistent toast */
        if (g_fs_retry_btn_ref) {
            lv_obj_t *sl = lv_obj_get_child(g_fs_retry_btn_ref, 0);
            if (sl) lv_label_set_text(sl, "Please call waiter");
        }
    }
}

static void fs_gen_bill_cb(lv_event_t *e)
{
    /* Transition to Bill screen directly after food served */
    sm_set(STATE_BILL);
}

static void food_received_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_label_set_text(lbl, "Marked Served");
    
    /* Call server to update dashboard to 'Served' */
    net_food_served(sm_get_order_id());
    
    /* Reveal the Bill and Order More buttons if they were hidden */
    lv_obj_t *card = lv_obj_get_parent(btn);
    if (card) {
        uint32_t i;
        for(i=0; i < lv_obj_get_child_cnt(card); i++) {
            lv_obj_t *c = lv_obj_get_child(card, i);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* Hide the 'Food Received' button itself once clicked */
    lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
}

static void fs_opa_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void fs_size_anim_cb(void *var, int32_t v)
{
    lv_obj_set_size((lv_obj_t *)var, v, v);
    lv_obj_align((lv_obj_t *)var, LV_ALIGN_TOP_MID, 0, 20);
}

static void build_food_served(void)
{
    scr_food_served = make_screen();
    lv_obj_t *card  = make_card(scr_food_served, 500, 340);
    lv_obj_center(card);
    add_card_accent(card, 500);

    lv_obj_t *circle = lv_obj_create(card);
    lv_obj_set_size(circle, 80, 80);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, COL_SUCCESS, 0);
    lv_obj_set_style_border_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_t *lbl_ck = make_label(circle, LV_SYMBOL_OK, lv_color_hex(0x0A0A0A), &lv_font_montserrat_32);
    lv_obj_center(lbl_ck);

    lv_obj_t *lbl_title = make_label(card, "Enjoy Your Meal!", COL_SUCCESS, &lv_font_montserrat_32);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 110);

    lv_obj_t *lbl_sub = make_label(card, "Need anything else while you dine?", COL_GREY, &lv_font_montserrat_16);
    lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 150);

    /* BTN 1: Order More */
    lv_obj_t *btn1 = make_amber_btn(card, LV_SYMBOL_PLUS "  ADD MORE ITEMS", 380, 50);
    lv_obj_align(btn1, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_add_event_cb(btn1, add_more_cb, LV_EVENT_CLICKED, NULL);

    /* BTN 2: Generate Bill */
    lv_obj_t *btn2 = make_amber_btn(card, LV_SYMBOL_CASH "  GET DIGITAL BILL", 380, 50);
    lv_obj_align(btn2, LV_ALIGN_TOP_MID, 0, 250);
    lv_obj_set_style_bg_color(btn2, COL_AMBER, 0);
    lv_obj_add_event_cb(btn2, fs_gen_bill_cb, LV_EVENT_CLICKED, NULL);
}

/* =====================================================================
 *  SCREEN 5 — BILL  (Bug 2 Fix: one-shot timer avoids double-call race)
 * ===================================================================== */
static void proceed_payment_cb(lv_event_t *e) { sm_set(STATE_PAYMENT_SELECT); }

static void load_bill_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    if (sm_get_order_id() < 0 || !lbl_bill_body) return;
    char *bill_json = net_request_bill(sm_get_order_id());
    if (!bill_json) {
        lv_label_set_text(lbl_bill_body, "TOTAL:  Rs. ---  (error)");
        return;
    }

    int sub = 0, gst = 0, total = 0;
    const char *ps;
    /* Flexible parsing for totals */
    ps = strstr(bill_json, "\"subtotal\"");
    if (ps) { ps = strchr(ps, ':'); if (ps) { ps++; while(*ps == ' ') ps++; sub = atoi(ps); } }
    ps = strstr(bill_json, "\"gst\"");
    if (ps) { ps = strchr(ps, ':'); if (ps) { ps++; while(*ps == ' ') ps++; gst = atoi(ps); } }
    ps = strstr(bill_json, "\"total\"");
    if (ps) { 
        ps = strchr(ps, ':'); 
        if (ps) { 
            ps++; while(*ps == ' ') ps++; 
            total = atoi(ps); 
            g_total_bill_rupees = total; /* Store for payment selection page */
        } 
    }

    /* ── Populate item rows in bill_items_col ── */
    if (bill_items_col) {
        lv_obj_clean(bill_items_col);
        /* Parse items array from bill JSON - SKIP ROOT OBJECT */
        const char *p = strstr(bill_json, "\"items\"");
        if (p) {
            p = strchr(p, '[');
            if (p) {
                while ((p = strchr(p, '{')) != NULL) {
                    const char *obj_end = find_obj_end(p);
                    if (!obj_end) break;
                    char iname[64] = ""; int qty = 0, price = 0;
                    const char *f;
                    /* Flexible item_name extraction — check both "item_name" and "name" */
                    f = strstr(p, "\"item_name\"");
                    if (!f || f > obj_end) f = strstr(p, "\"name\"");
                    if (f && f < obj_end) {
                        f = strchr(f, ':');
                        if (f) {
                            f++; while (*f == ' ' || *f == '"') f++;
                            const char *q = strchr(f, '"');
                            if (q && q < obj_end) {
                                int l = (int)(q - f);
                                if (l >= 64) l = 63;
                                memcpy(iname, f, l); iname[l] = 0;
                            }
                        }
                    }
                    /* Flexible qty extraction */
                    f = strstr(p, "\"qty\"");
                    if (f && f < obj_end) { f = strchr(f, ':'); if (f) { f++; while(*f == ' ') f++; qty = atoi(f); } }
                    /* Flexible price extraction */
                    f = strstr(p, "\"price\"");
                    if (f && f < obj_end) { f = strchr(f, ':'); if (f) { f++; while(*f == ' ') f++; price = atoi(f); } }
                    
                    if (iname[0] && qty > 0) {
                        lv_obj_t *row_cont = lv_obj_create(bill_items_col);
                        lv_obj_set_size(row_cont, 580, 24);
                        lv_obj_set_style_pad_all(row_cont, 0, 0);
                        lv_obj_set_style_bg_opa(row_cont, LV_OPA_TRANSP, 0);
                        lv_obj_set_style_border_opa(row_cont, LV_OPA_TRANSP, 0);
                        
                        /* Perfectly Aligned Columns */
                        lv_obj_t *ln = make_label(row_cont, iname, COL_WHITE, &lv_font_montserrat_14);
                        lv_obj_align(ln, LV_ALIGN_LEFT_MID, 0, 0);
                        lv_label_set_long_mode(ln, LV_LABEL_LONG_DOT);
                        lv_obj_set_width(ln, 280);
                        
                        char buf[16]; snprintf(buf,sizeof(buf),"%d",qty);
                        lv_obj_t *lq = make_label(row_cont, buf, COL_GREY, &lv_font_montserrat_14);
                        lv_obj_align(lq, LV_ALIGN_LEFT_MID, 300, 0);
                        
                        snprintf(buf,sizeof(buf),"%d",price);
                        lv_obj_t *lp = make_label(row_cont, buf, COL_GREY, &lv_font_montserrat_14);
                        lv_obj_align(lp, LV_ALIGN_LEFT_MID, 380, 0);
                        
                        snprintf(buf,sizeof(buf),"%d",qty*price);
                        lv_obj_t *lt = make_label(row_cont, buf, COL_WHITE, &lv_font_montserrat_14);
                        lv_obj_align(lt, LV_ALIGN_RIGHT_MID, 0, 0);
                    }
                    p = obj_end;
                }
            }
        }
    }

    /* ── Update totals ── */
    char buf[64];
    if (lbl_bill_sub) {
        snprintf(buf, sizeof(buf), "Subtotal:                   Rs. %d", sub);
        lv_label_set_text(lbl_bill_sub, buf);
    }
    if (lbl_bill_gst) {
        snprintf(buf, sizeof(buf), "GST (5%%):                   Rs. %d", gst);
        lv_label_set_text(lbl_bill_gst, buf);
    }
    snprintf(buf, sizeof(buf), "TOTAL:                      Rs. %d", total);
    lv_label_set_text(lbl_bill_body, buf);

    /* ── Push amount to payment screens ── */
    snprintf(buf, sizeof(buf), "Amount Due: Rs. %d", total);
    if (lbl_paysel_amount) lv_label_set_text(lbl_paysel_amount, buf);
    if (lbl_upi_amount)    lv_label_set_text(lbl_upi_amount, buf);
    
    if (lbl_cash_amount) {
        snprintf(buf, sizeof(buf), "Rs. %d", total);
        lv_label_set_text(lbl_cash_amount, buf);
    }

    /* Timestamp */
    if (lbl_bill_time) {
        /* Use LVGL tick for a rough time display */
        lv_label_set_text(lbl_bill_time, "Thank you!");
    }

    free(bill_json);
}


/* Bill dash separator line helper */
static lv_obj_t *bill_dash(lv_obj_t *parent, int y_ofs)
{
    lv_obj_t *d = make_label(parent,
        "- - - - - - - - - - - - - - - - - - - - - - - - - - - -",
        lv_color_hex(0x334155), &lv_font_montserrat_10);
    lv_obj_align(d, LV_ALIGN_TOP_LEFT, 0, y_ofs);
    return d;
}

static void build_bill(void)
{
    scr_bill = make_screen();

    /* Receipt card — white-paper look on dark bg */
    lv_obj_t *card = lv_obj_create(scr_bill);
    lv_obj_set_size(card, 620, 460);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, COL_AMBER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_color(card, COL_AMBER, 0);
    lv_obj_set_style_shadow_opa(card, 40, 0);

    /* ── Restaurant header ── */
    lv_obj_t *lbl_rest = make_label(card, "AutoDine Restaurant",
                                    COL_AMBER, &lv_font_montserrat_22);
    lv_obj_align(lbl_rest, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *lbl_sub_hdr = make_label(card,
        "GSTIN: 29XXXXXXXXXXZ1  |  Ph: +91-XXXXXXXXXX",
        COL_GREY, &lv_font_montserrat_10);
    lv_obj_align(lbl_sub_hdr, LV_ALIGN_TOP_MID, 0, 28);

    bill_dash(card, 44);

    /* ── Table Headers (Perfectly matches load_bill_cb row alignment) ── */
    lv_obj_t *hdr_cont = lv_obj_create(card);
    lv_obj_set_size(hdr_cont, 580, 20);
    lv_obj_align(hdr_cont, LV_ALIGN_TOP_LEFT, 0, 56);
    lv_obj_set_style_bg_opa(hdr_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(hdr_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(hdr_cont, 0, 0);

    lv_obj_t *h1 = make_label(hdr_cont, "ITEM", COL_GREY, &lv_font_montserrat_12);
    lv_obj_align(h1, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *h2 = make_label(hdr_cont, "QTY", COL_GREY, &lv_font_montserrat_12);
    lv_obj_align(h2, LV_ALIGN_LEFT_MID, 300, 0);
    lv_obj_t *h3 = make_label(hdr_cont, "RATE", COL_GREY, &lv_font_montserrat_12);
    lv_obj_align(h3, LV_ALIGN_LEFT_MID, 380, 0);
    lv_obj_t *h4 = make_label(hdr_cont, "AMOUNT", COL_GREY, &lv_font_montserrat_12);
    lv_obj_align(h4, LV_ALIGN_RIGHT_MID, 0, 0);

    bill_dash(card, 68);

    /* ── Scrollable item column ── */
    bill_items_col = lv_obj_create(card);
    lv_obj_set_size(bill_items_col, 580, 180);
    lv_obj_align(bill_items_col, LV_ALIGN_TOP_LEFT, 0, 76);
    lv_obj_set_style_bg_opa(bill_items_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(bill_items_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(bill_items_col, 0, 0);
    lv_obj_set_flex_flow(bill_items_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bill_items_col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(bill_items_col, 2, 0);
    lv_obj_set_scrollbar_mode(bill_items_col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(bill_items_col, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    bill_dash(card, 262);

    /* ── Totals block ── */
    lbl_bill_sub = make_label(card, "Subtotal:                   Rs. ---",
                              COL_WHITE, &lv_font_montserrat_12);
    lv_obj_align(lbl_bill_sub, LV_ALIGN_TOP_LEFT, 0, 272);

    lbl_bill_gst = make_label(card, "GST (5%):                   Rs. ---",
                              COL_GREY, &lv_font_montserrat_12);
    lv_obj_align(lbl_bill_gst, LV_ALIGN_TOP_LEFT, 0, 290);

    bill_dash(card, 308);

    lbl_bill_body = make_label(card, "TOTAL:                      Rs. ---",
                               COL_AMBER, &lv_font_montserrat_18);
    lv_obj_align(lbl_bill_body, LV_ALIGN_TOP_LEFT, 0, 316);

    lbl_bill_time = make_label(card, "", COL_GREY, &lv_font_montserrat_10);
    lv_obj_align(lbl_bill_time, LV_ALIGN_TOP_RIGHT, 0, 320);

    /* ── Pay button ── */
    lv_obj_t *btn_pay = make_amber_btn(card, LV_SYMBOL_CHARGE "  Proceed to Payment", 580, 54);
    lv_obj_align(btn_pay, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(btn_pay, proceed_payment_cb, LV_EVENT_CLICKED, NULL);
}

/* =====================================================================
 *  SCREEN 6 — PAYMENT SELECT
 * ===================================================================== */

static uint32_t paysel_entry_ms = 0;

static void upi_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_tick_elaps(paysel_entry_ms) < 400) return;

    g_razorpay_url[0] = '\0';
    sm_set(STATE_PAYMENT_UPI);
}
static void cash_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_tick_elaps(paysel_entry_ms) < 400) return;

    g_pending_cash_select = true; sm_set(STATE_PAYMENT_CASH);
}

static void build_payment_select(void)
{
    scr_paysel = make_screen();
    make_label(scr_paysel, "Payment", COL_WHITE, &lv_font_montserrat_20);
    lv_obj_align(lv_obj_get_child(scr_paysel, 0), LV_ALIGN_TOP_MID, 0, 20);

    lbl_paysel_amount = make_label(scr_paysel, "Amount Due: Rs. ---",
                                 COL_AMBER, &lv_font_montserrat_32);
    lv_obj_align(lbl_paysel_amount, LV_ALIGN_TOP_MID, 0, 52);

    make_label(scr_paysel, "Select your preferred payment method",
               COL_GREY, &lv_font_montserrat_14);
    lv_obj_align(lv_obj_get_child(scr_paysel, 2), LV_ALIGN_TOP_MID, 0, 100);

    /* --- UPI card (Gold Neon Glow) --- */
    lv_obj_t *cu = make_card(scr_paysel, 280, 240);
    lv_obj_align(cu, LV_ALIGN_CENTER, -160, 40);
    lv_obj_set_style_bg_color(cu, lv_color_hex(0x1e1b4b), 0); /* deep indigo */
    lv_obj_set_style_border_color(cu, COL_AMBER, 0);
    lv_obj_set_style_shadow_color(cu, COL_AMBER, 0);
    lv_obj_set_style_shadow_width(cu, 40, 0);
    lv_obj_set_style_shadow_opa(cu, 100, 0);
    lv_obj_add_event_cb(cu, upi_cb, LV_EVENT_CLICKED, NULL);

    {
        /* Styled Icon for UPI */
        lv_obj_t *icon = make_label(cu, LV_SYMBOL_CHARGE, COL_AMBER, &lv_font_montserrat_48);
        lv_obj_center(icon);
        lv_obj_set_style_translate_y(icon, -40, 0);
        
        /* Floating Animation */
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, icon);
        lv_anim_set_values(&a, -40, -55);
        lv_anim_set_time(&a, 1500);
        lv_anim_set_playback_time(&a, 1500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&a, anim_translate_y_cb);
        lv_anim_start(&a);

        make_label(cu, "UPI / QR SCAN", COL_WHITE, &lv_font_montserrat_20);
        lv_obj_align(lv_obj_get_child(cu, 1), LV_ALIGN_CENTER, 0, 25);
        
        make_label(cu, "Instant & Secure", COL_AMBER2, &lv_font_montserrat_12);
        lv_obj_align(lv_obj_get_child(cu, 2), LV_ALIGN_CENTER, 0, 55);
    }

    /* --- Cash card (Emerald Neon Glow) --- */
    lv_obj_t *cc = make_card(scr_paysel, 280, 240);
    lv_obj_align(cc, LV_ALIGN_CENTER, 160, 40);
    lv_obj_set_style_bg_color(cc, lv_color_hex(0x064e3b), 0);
    lv_obj_set_style_border_color(cc, COL_SUCCESS, 0);
    lv_obj_set_style_shadow_color(cc, COL_SUCCESS, 0);
    lv_obj_set_style_shadow_width(cc, 40, 0);
    lv_obj_set_style_shadow_opa(cc, 100, 0);
    lv_obj_add_event_cb(cc, cash_cb, LV_EVENT_CLICKED, NULL);

    {
        /* Styled Icon for Cash */
        lv_obj_t *icon = make_label(cc, LV_SYMBOL_OK, COL_SUCCESS, &lv_font_montserrat_48);
        lv_obj_center(icon);
        lv_obj_set_style_translate_y(icon, -40, 0);

        /* Floating Animation */
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, icon);
        lv_anim_set_values(&a, -40, -55);
        lv_anim_set_time(&a, 1800);
        lv_anim_set_playback_time(&a, 1800);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&a, anim_translate_y_cb);
        lv_anim_start(&a);

        make_label(cc, "CASH / CARD", COL_WHITE, &lv_font_montserrat_20);
        lv_obj_align(lv_obj_get_child(cc, 1), LV_ALIGN_CENTER, 0, 25);
        
        make_label(cc, "Pay at Counter", COL_GREY, &lv_font_montserrat_12);
        lv_obj_align(lv_obj_get_child(cc, 2), LV_ALIGN_CENTER, 0, 55);
    }
}

/* =====================================================================
 *  SCREEN 7A — UPI
 * ===================================================================== */

/* Deferred UPI fetch — called via one-shot timer to avoid blocking in LVGL */
static void upi_deferred_fetch_cb(lv_timer_t *t)
{
    lv_timer_del(t);

    /* 1. Tell server we chose UPI */
    net_select_payment(sm_get_order_id(), "upi");

    /* 2. Fetch Razorpay Payment Link */
    if (g_razorpay_url[0] == '\0') {
        char *json = net_create_razorpay_order(sm_get_order_id());
        if (json) {
            /* Parse qr_url with flexible spacing */
            const char *k = strstr(json, "\"qr_url\"");
            if (k) {
                k = strchr(k, ':');
                if (k) {
                    k++; while(*k == ' ') k++;
                    if (*k == '"') k++;
                    const char *end = strchr(k, '"');
                    int len = end ? (int)(end - k) : 0;
                    if (len > 0 && len < (int)sizeof(g_razorpay_url)) {
                        memcpy(g_razorpay_url, k, len);
                        g_razorpay_url[len] = '\0';
                    }
                }
            }
            free(json);
        }
    }

    /* 3. Update QR display */
    if (g_razorpay_url[0] != '\0' && upi_qr_obj) {
        lv_qrcode_update(upi_qr_obj, g_razorpay_url, strlen(g_razorpay_url));
        if (upi_spinner) lv_obj_add_flag(upi_spinner, LV_OBJ_FLAG_HIDDEN);
        {
            lv_anim_t a; lv_anim_init(&a);
            lv_anim_set_var(&a, upi_qr_obj);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_zoom);
            lv_anim_set_values(&a, 0, 256);
            lv_anim_set_time(&a, 400);
            lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
            lv_anim_start(&a);
        }
        if (upi_qr_box) {
            lv_anim_t bp; lv_anim_init(&bp);
            lv_anim_set_var(&bp, upi_qr_box);
            lv_anim_set_exec_cb(&bp, (lv_anim_exec_xcb_t)lv_obj_set_style_border_opa);
            lv_anim_set_values(&bp, 100, 255);
            lv_anim_set_time(&bp, 1200);
            lv_anim_set_playback_time(&bp, 1200);
            lv_anim_set_repeat_count(&bp, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&bp);
        }
        net_buzz(2);
    } else {
        if (upi_spinner) lv_obj_add_flag(upi_spinner, LV_OBJ_FLAG_HIDDEN);
        if (upi_qr_label) {
            lv_obj_clear_flag(upi_qr_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(upi_qr_label, "Staff Coming\nfor Payment\n\n(QR Unavailable)");
        }
        if (lbl_payment_result) {
            lv_label_set_text(lbl_payment_result, "Manual payment - staff notified");
            lv_obj_set_style_text_color(lbl_payment_result, COL_AMBER, 0);
        }
        net_buzz(1);
    }
}

static void upi_goto_feedback_cb(lv_timer_t *t) {
    lv_timer_del(t); sm_set(STATE_FEEDBACK);
}

/* UPI poll timer: sets a flag for deferred network call, NEVER calls HTTP here */
static void upi_poll_cb(lv_timer_t *t)
{
    if (upi_poll_timer == NULL) return;
    upi_timeout_count++;
    /* Set flag — actual HTTP done in ui_check_deferred outside mutex */
    g_pending_upi_poll = true;
}


static void build_upi(void)
{
    scr_upi = make_screen();

    /* Title with fade-in */
    { lv_obj_t *_l = make_label(scr_upi, "Scan QR & Pay via UPI",
                                  COL_WHITE, &lv_font_montserrat_22);
      lv_obj_align(_l, LV_ALIGN_TOP_MID, 0, 14);
      lv_obj_fade_in(_l, 300, 0); }

    /* Amount with slide-up animation */
    lbl_upi_amount = make_label(scr_upi, "Rs. ---",
                                COL_AMBER, &lv_font_montserrat_32);
    lv_obj_align(lbl_upi_amount, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_fade_in(lbl_upi_amount, 300, 100);

    /* QR container box */
    upi_qr_box = lv_obj_create(scr_upi);
    lv_obj_set_size(upi_qr_box, 240, 240);
    lv_obj_set_style_bg_color(upi_qr_box, COL_WHITE, 0);
    lv_obj_set_style_radius(upi_qr_box, 16, 0);
    lv_obj_set_style_border_color(upi_qr_box, COL_AMBER, 0);
    lv_obj_set_style_border_width(upi_qr_box, 3, 0);
    lv_obj_set_style_border_opa(upi_qr_box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(upi_qr_box, 8, 0);
    lv_obj_align(upi_qr_box, LV_ALIGN_CENTER, -120, 20);

    /* LVGL native QR code object inside the box (Reduced size for better stability) */
    upi_qr_obj = lv_qrcode_create(upi_qr_box, 160, 
                    lv_color_hex(0x000000), COL_WHITE);
    lv_obj_center(upi_qr_obj);
    /* Initially set to a placeholder string */
    lv_qrcode_update(upi_qr_obj, "loading...", 10);
    /* Start zoomed out, will animate in when data arrives */
    lv_obj_set_style_transform_zoom(upi_qr_obj, 0, 0);
    
    /* Loading spinner (shown until QR data arrives) */
    upi_spinner = lv_spinner_create(upi_qr_box, 1000, 60);
    lv_obj_set_size(upi_spinner, 60, 60);
    lv_obj_center(upi_spinner);
    lv_obj_set_style_arc_color(upi_spinner, COL_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(upi_spinner, COL_GREY, LV_PART_MAIN);

    /* Fallback text label (hidden by default, shown on error) */
    upi_qr_label = make_label(upi_qr_box, "",
                              lv_color_hex(0x333333), &lv_font_montserrat_12);
    lv_obj_set_style_text_align(upi_qr_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(upi_qr_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(upi_qr_label, 200);
    lv_obj_center(upi_qr_label);
    lv_obj_add_flag(upi_qr_label, LV_OBJ_FLAG_HIDDEN);

    /* Instructions card on right side */
    upi_info_card = make_card(scr_upi, 220, 240);
    lv_obj_align(upi_info_card, LV_ALIGN_CENTER, 105, 20);
    lv_obj_set_style_pad_all(upi_info_card, 12, 0);

    { lv_obj_t *_l = make_label(upi_info_card, "How to pay:",
                                  COL_AMBER, &lv_font_montserrat_14);
      lv_obj_align(_l, LV_ALIGN_TOP_LEFT, 0, 0); }
    { lv_obj_t *_l = make_label(upi_info_card,
        "1. Open any UPI app\n"
        "2. Tap Scan QR\n"
        "3. Scan the QR code\n"
        "4. Amount is pre-filled\n"
        "5. Complete payment\n\n"
        "Payment auto-confirms\nwithin seconds.",
        COL_GREY, &lv_font_montserrat_10);
      lv_label_set_long_mode(_l, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(_l, 196);
      lv_obj_align(_l, LV_ALIGN_TOP_LEFT, 0, 26); }

    /* Payment result label */
    lbl_payment_result = make_label(scr_upi, "", COL_AMBER, &lv_font_montserrat_18);
    lv_obj_align(lbl_payment_result, LV_ALIGN_BOTTOM_MID, 0, -68);

    { lv_obj_t *_l = make_label(scr_upi,
                                  "Awaiting payment...",
                                  COL_GREY, &lv_font_montserrat_12);
      lv_obj_align(_l, LV_ALIGN_BOTTOM_MID, 0, -50); }

    /* Call Waiter button — always visible at bottom of UPI screen */
    lv_obj_t *btn_cw = lv_btn_create(scr_upi);
    lv_obj_set_size(btn_cw, 220, 40);
    lv_obj_align(btn_cw, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(btn_cw, COL_ERROR, 0);
    lv_obj_set_style_radius(btn_cw, 10, 0);
    lv_obj_set_style_border_opa(btn_cw, LV_OPA_TRANSP, 0);
    lv_obj_t *cw_lbl = lv_label_create(btn_cw);
    lv_label_set_text(cw_lbl, LV_SYMBOL_BELL "  Call Waiter");
    lv_obj_set_style_text_color(cw_lbl, COL_WHITE, 0);
    lv_obj_set_style_text_font(cw_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(cw_lbl);
    lv_obj_add_event_cb(btn_cw, (lv_event_cb_t)fs_call_waiter_cb, LV_EVENT_CLICKED, NULL);
}

/* =====================================================================
 *  SCREEN 7B — CASH
 * ===================================================================== */
static void cash_poll_cb(lv_timer_t *t)
{
    if (cash_poll_timer == NULL) return;
    char status[32];
    net_get_payment_status(sm_get_order_id(), status, sizeof(status));
    
    /* SYNC FIX: check for "paid" (set by chef verify_payment/verify_manual) */
    if (strcmp(status, "paid") == 0) {
        safe_timer_del(&cash_poll_timer);
        net_buzz(3);
        sm_set(STATE_FEEDBACK);
    }
}

static void build_cash(void)
{
    scr_cash = make_screen();
    
    cash_circle = make_card(scr_cash, 440, 260); /* Recycled cash_circle as the main card ID */
    lv_obj_center(cash_circle);
    lv_obj_set_style_border_color(cash_circle, COL_SUCCESS, 0);
    lv_obj_set_style_border_width(cash_circle, 2, 0);
    lv_obj_set_style_shadow_color(cash_circle, COL_SUCCESS, 0);
    lv_obj_set_style_shadow_width(cash_circle, 40, 0);
    lv_obj_set_style_shadow_opa(cash_circle, 60, 0);
    
    lv_obj_t *icon = make_label(cash_circle, "CASH PAYMENT", COL_SUCCESS, &lv_font_montserrat_22);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 20);
    
    cash_amt_lbl = make_label(cash_circle, "Rs. ---", COL_WHITE, &lv_font_montserrat_48);
    lv_obj_align(cash_amt_lbl, LV_ALIGN_CENTER, 0, -5);
    lbl_cash_amount = cash_amt_lbl;  /* share the pointer */
    
    lv_obj_t *lbl_info = make_label(cash_circle, "Please pay at the counter", COL_WHITE, &lv_font_montserrat_22);
    lv_obj_align(lbl_info, LV_ALIGN_BOTTOM_MID, 0, -55);

    lv_obj_t *lbl_wait = make_label(cash_circle, "Awaiting staff confirmation...", COL_GREY, &lv_font_montserrat_14);
    lv_obj_align(lbl_wait, LV_ALIGN_BOTTOM_MID, 0, -20);
}

/* =====================================================================
 *  SCREEN 8 — FEEDBACK  (Bug 5 Fix: full reset on re-entry)
 * ===================================================================== */
static const char *star_rating_labels[] = {"Poor","Fair","Good","Great","Excellent"};
static lv_obj_t *feedback_bar = NULL;

static void star_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_stars = idx + 1;
    for (int i = 0; i < 5; i++) {
        if (!star_btns[i]) continue;
        bool filled = (i <= idx);
        lv_obj_set_style_bg_color(star_btns[i], filled ? COL_AMBER : COL_CARD, 0);
        lv_obj_t *sl = lv_obj_get_child(star_btns[i], 0);
        if (sl) lv_obj_set_style_text_color(sl, filled ? lv_color_hex(0x0A0A0A) : COL_AMBER, 0);
    }
    if (lbl_star_rating) lv_label_set_text(lbl_star_rating, star_rating_labels[idx]);
    /* BUG 5: High-impact bounce scale animate the tapped star */
    if (star_btns[idx]) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, star_btns[idx]);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_zoom);
        lv_anim_set_values(&a, 256, 400); /* 1.56x Zoom */
        lv_anim_set_time(&a, 200);
        lv_anim_set_playback_time(&a, 150);
        lv_anim_start(&a);
    }
}

static void feedback_timer_cb(lv_timer_t *t)
{
    fb_seconds--;
    if (lbl_fb_countdown) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Auto-submitting in %ds", fb_seconds);
        lv_label_set_text(lbl_fb_countdown, buf);
    }
    if (feedback_bar) lv_bar_set_value(feedback_bar, fb_seconds, LV_ANIM_OFF);
    if (fb_seconds <= 0) {
        if (t) { lv_timer_del(t); feedback_timer = NULL; }
        /* BUG 5 FIX: Auto-submit also requires state reset */
        cart_clear();
        sm_set_order_id(0);
        g_append_mode   = false;
        g_star_rating   = 0;
        g_stars         = 0;
        g_last_status[0] = '\0';
        g_razorpay_url[0] = '\0';
        sm_set(STATE_SPLASH);
    }
}

static void submit_feedback_cb(lv_event_t *e)
{
    const char *comment = feedback_ta ? lv_textarea_get_text(feedback_ta) : "";
    net_submit_feedback(sm_get_order_id(), g_stars, comment);
    safe_timer_del(&feedback_timer);
    
    /* Full Reset */
    cart_clear();
    sm_set_order_id(0);
    g_append_mode   = false;
    g_star_rating   = 0;
    g_stars         = 0;
    g_last_status[0] = '\0';
    g_razorpay_url[0] = '\0';
    sm_set(STATE_SPLASH);
}

/* Keyboard show/hide — also reposition submit button to stay visible */
static lv_obj_t *fb_keyboard = NULL;

static void fb_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (!fb_keyboard) return;

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(fb_keyboard, LV_OBJ_FLAG_HIDDEN);
        
        /* Slide card up by 150 pixels so text area is visible above keyboard */
        lv_obj_t *card = lv_obj_get_parent(feedback_ta);
        if (card) lv_obj_set_y(card, -120);

        /* Permanently stop time (cancel timeout) */
        if (feedback_timer) {
            lv_timer_del(feedback_timer);
            feedback_timer = NULL;
        }
        
        /* Hide stuff so it doesn't overly on keyboard */
        if (fb_submit_btn)    lv_obj_add_flag(fb_submit_btn, LV_OBJ_FLAG_HIDDEN);
        if (feedback_bar)     lv_obj_add_flag(feedback_bar, LV_OBJ_FLAG_HIDDEN);
        if (lbl_fb_countdown) lv_obj_add_flag(lbl_fb_countdown, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(fb_keyboard, LV_OBJ_FLAG_HIDDEN);
        
        /* Reset card position */
        lv_obj_t *card = lv_obj_get_parent(feedback_ta);
        if (card) lv_obj_center(card);

        /* Restore submit button once keyboard is closed */
        if (fb_submit_btn)    lv_obj_clear_flag(fb_submit_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY) {
        /* User pressed Enter/Checkmark on keyboard, submit immediately */
        submit_feedback_cb(NULL);
    }
}

static void build_feedback(void)
{
    scr_feedback = make_screen();
    lv_obj_set_style_bg_color(scr_feedback, COL_BG, 0);
    fb_seconds = 20;

    /* Main Card */
    lv_obj_t *card = make_card(scr_feedback, 680, 420);
    lv_obj_center(card);
    add_card_accent(card, 680);

    lv_obj_t *lbl_title = make_label(card, "How was your experience?", COL_WHITE, &lv_font_montserrat_28);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 10);

    /* ── 5-star row ── */
    lv_obj_t *star_row = lv_obj_create(card);
    lv_obj_set_size(star_row, 600, 100);
    lv_obj_set_style_bg_opa(star_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(star_row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(star_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(star_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(star_row, 20, 0);
    lv_obj_align(star_row, LV_ALIGN_TOP_MID, 0, 50);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *sb = lv_btn_create(star_row);
        lv_obj_set_size(sb, 80, 80);
        lv_obj_set_style_bg_color(sb, COL_CARD2, 0);
        lv_obj_set_style_radius(sb, 12, 0);
        lv_obj_set_style_border_color(sb, COL_AMBER, 0);
        lv_obj_set_style_border_width(sb, 1, 0);
        
        lv_obj_t *sl = lv_label_create(sb);
        lv_label_set_text(sl, LV_SYMBOL_STAR);
        lv_obj_set_style_text_color(sl, COL_AMBER, 0);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_32, 0);
        lv_obj_center(sl);
        star_btns[i] = sb;
        lv_obj_add_event_cb(sb, star_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    lbl_star_rating = make_label(card, "Tap to rate", COL_AMBER, &lv_font_montserrat_18);
    lv_obj_align(lbl_star_rating, LV_ALIGN_TOP_MID, 0, 150);

    /* Comment area */
    feedback_ta = lv_textarea_create(card);
    lv_obj_set_size(feedback_ta, 600, 100);
    lv_obj_align(feedback_ta, LV_ALIGN_TOP_MID, 0, 185);
    lv_obj_set_style_bg_color(feedback_ta, COL_CARD2, 0);
    lv_obj_set_style_text_color(feedback_ta, COL_WHITE, 0);
    lv_textarea_set_placeholder_text(feedback_ta, "Optional: Share your thoughts...");
    lv_obj_add_event_cb(feedback_ta, fb_ta_event_cb, LV_EVENT_ALL, NULL);

    fb_submit_btn = make_amber_btn(card, "SUBMIT FEEDBACK", 600, 50);
    lv_obj_align(fb_submit_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(fb_submit_btn, submit_feedback_cb, LV_EVENT_CLICKED, NULL);

    /* Invisible keyboard container (to be shown by ta_event) */
    fb_keyboard = lv_keyboard_create(scr_feedback);
    lv_obj_set_size(fb_keyboard, 800, 240);
    lv_obj_align(fb_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(fb_keyboard, feedback_ta);
    lv_obj_add_flag(fb_keyboard, LV_OBJ_FLAG_HIDDEN);
}

/* =====================================================================
 *  PUBLIC API
 * ===================================================================== */
void ui_init(void)
{
    build_splash(); build_menu(); build_order_placed(); build_food_ready();
    build_food_served();
    build_bill(); build_payment_select(); build_upi(); build_cash(); build_feedback();
}

void ui_show_screen(app_state_t state)
{
    net_log("[UI] Transition to state %d\n", state);
    /* Safe Cleanup: Central authority for timer destruction */
    safe_timer_del(&poll_timer);
    safe_timer_del(&upi_poll_timer);
    safe_timer_del(&cash_poll_timer);
    safe_timer_del(&feedback_timer);

    switch (state) {
        case STATE_SPLASH:
            lv_scr_load_anim(scr_splash, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, false);
            break;
        case STATE_MENU: {
            lv_scr_load_anim(scr_menu, LV_SCR_LOAD_ANIM_MOVE_LEFT, 400, 0, false);
            /* BUG 1 FIX: update banner/title logic */
            if (lbl_append_banner) {
                if (g_append_mode) {
                    char banner_buf[64];
                    snprintf(banner_buf, sizeof(banner_buf),
                             "Adding to Order #%d", sm_get_order_id());
                    lv_obj_t *btxt = lv_obj_get_child(lbl_append_banner, 0);
                    if (btxt) lv_label_set_text(btxt, banner_buf);
                    lv_obj_clear_flag(lbl_append_banner, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(lbl_append_banner, LV_OBJ_FLAG_HIDDEN);
                }
            }
            /* BUG 1 FIX: update cart sidebar title */
            if (lbl_cart_title)
                lv_label_set_text(lbl_cart_title,
                                  g_append_mode ? "NEW ITEMS" : "YOUR ORDER");
            /* BUG 1 FIX: update place-order button label */
            if (btn_place_order) {
                lv_obj_t *btn_lbl = lv_obj_get_child(btn_place_order, 0);
                if (btn_lbl)
                    lv_label_set_text(btn_lbl,
                                      g_append_mode ? "ADD TO ORDER" : "PLACE ORDER");
            }
            refresh_cart_panel();
            break;
        }
        case STATE_ORDER_PLACED:
            lv_scr_load_anim(scr_placed, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
            if (lbl_order_id_placed && sm_get_order_id() > 0) {
                char buf[48];
                snprintf(buf, sizeof(buf), "Order #%d sent to kitchen", sm_get_order_id());
                lv_label_set_text(lbl_order_id_placed, buf);
            }
            /* BUG 1 FIX: keep g_poll_timer in sync with poll_timer */
            poll_timer = lv_timer_create(order_poll_cb, ORDER_POLL_INTERVAL_MS, NULL);
            break;
        case STATE_FOOD_READY:
            lv_scr_load_anim(scr_ready, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
            /* Premium Ready Animation: bounce the card */
            {
               lv_anim_t a; lv_anim_init(&a);
               lv_anim_set_var(&a, lv_scr_act());
               lv_anim_set_values(&a, 0, -15);
               lv_anim_set_time(&a, 600);
               lv_anim_set_playback_time(&a, 600);
               lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
               lv_anim_start(&a);
            }
            break;
        case STATE_FOOD_SERVED:
            if (scr_food_served) { lv_obj_del(scr_food_served); scr_food_served = NULL; }
            build_food_served();
            lv_scr_load_anim(scr_food_served, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 500, 0, false);
            break;
        case STATE_BILL:
            lv_scr_load_anim(scr_bill, LV_SCR_LOAD_ANIM_MOVE_LEFT, 450, 0, false);
            if (lbl_bill_body) lv_label_set_text(lbl_bill_body, "Processing your digital bill...");
            { lv_timer_t *bt = lv_timer_create(load_bill_cb, 350, NULL);
              if (bt) lv_timer_set_repeat_count(bt, 1); }
            break;
        case STATE_PAYMENT_SELECT:
        {
            paysel_entry_ms = lv_tick_get(); /* Mark entry time for touch guard */
            if (lbl_paysel_amount) {
                char b[64]; snprintf(b, sizeof(b), "Amount Due: Rs. %d", g_total_bill_rupees);
                lv_label_set_text(lbl_paysel_amount, b);
            }
            lv_scr_load_anim(scr_paysel, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, false);
            /* Bounce Animation on Payment Options */
            uint32_t i;
            for(i=0; i<lv_obj_get_child_cnt(scr_paysel); i++) {
                lv_obj_t *c = lv_obj_get_child(scr_paysel, i);
                /* Match card width 280 */
                if(lv_obj_get_width(c) == 280) { 
                    lv_anim_t a; lv_anim_init(&a);
                    lv_anim_set_var(&a, c);
                    lv_anim_set_exec_cb(&a, anim_zoom_cb);
                    lv_anim_set_values(&a, 0, 256);
                    lv_anim_set_time(&a, 600);
                    lv_anim_set_delay(&a, 100 + (i * 100));
                    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
                    lv_anim_start(&a);
                }
            }
            break;
        }
        case STATE_PAYMENT_UPI:
            lv_scr_load_anim(scr_upi, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, false);
            if (lbl_payment_result) lv_label_set_text(lbl_payment_result, "");
            upi_timeout_count = 0;  /* reset 5-min timer */
            
            /* Show spinner while fetching */
            if (upi_spinner) lv_obj_clear_flag(upi_spinner, LV_OBJ_FLAG_HIDDEN);
            if (upi_qr_obj) lv_obj_set_style_transform_zoom(upi_qr_obj, 0, 0);

            /* Deferred HTTP: Set flag to run in loop OUTSIDE main mutex */
            g_pending_upi_fetch = true;
            
            if (upi_info_card) {
                lv_obj_set_style_border_color(upi_info_card, COL_AMBER, 0);
                lv_obj_set_style_border_width(upi_info_card, 1, 0);
            }
            upi_poll_timer = lv_timer_create(upi_poll_cb, 3000, NULL);
            break;
        case STATE_PAYMENT_CASH:
            lv_scr_load_anim(scr_cash, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, false);
            cash_poll_timer = lv_timer_create(cash_poll_cb, PAYMENT_POLL_MS, NULL);
            break;
        case STATE_FEEDBACK:
            fb_seconds = 20;
            if (lbl_fb_countdown) lv_label_set_text(lbl_fb_countdown, "Thank you! Redirecting in 20s");
            g_stars = 0;
            lv_scr_load_anim(scr_feedback, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, false);
            feedback_timer = lv_timer_create(feedback_timer_cb, 1000, NULL);
            break;
        default: break;
    }
}

/* Deferred action handler — called from loop() OUTSIDE lvgl_acquire.
 * Any LVGL calls here must be wrapped in lvgl_acquire/release. */
void ui_check_deferred(void)
{
    /* ---- Screen transitions (safe, no network) ---- */
    if (g_goto_menu) {
        g_goto_menu = false;
        lvgl_acquire();
        sm_set(STATE_MENU);
        lvgl_release();
    }
    if (g_goto_splash) {
        g_goto_splash = false;
        lvgl_acquire();
        sm_set(STATE_SPLASH);
        lvgl_release();
    }
    if (g_pending_cash_select) {
        /* No network held while calling this — safest */
        g_pending_cash_select = false;
        net_select_payment(sm_get_order_id(), "cash");
        net_buzz(2);
    }
    if (g_pending_food_served) {
        g_pending_food_served = false;
        net_food_served(sm_get_order_id());
        net_buzz(2);
    }

    /* ---- Deferred Action: Buzzer Pattern ---- */
    if (g_pending_buzz) {
        int pat = g_pending_buzz;
        g_pending_buzz = 0;
        net_buzz(pat);
    }

    /* ---- Deferred Action: WiFi Ticker ---- */
    uint32_t now = lv_tick_get();
    if (now - last_wifi_ms >= 5000) {
        last_wifi_ms = now;
        bool ok = net_is_wifi_ok();
        lvgl_acquire();
        ui_set_wifi_connected(ok);
        lvgl_release();
    }

    /* ---- Deferred UPI link creation (blocking HTTP) ---- */
    if (g_pending_upi_fetch) {
        g_pending_upi_fetch = false;
        
        /* Small delay to let LVGL transition finish safely before network hit */
        net_delay(200); 

        int oid = sm_get_order_id();
        net_log("[NET] Fetching UPI link for order #%d\n", oid);

        /* Network calls — NO mutex held */
        net_select_payment(oid, "upi");
        char *json = net_create_razorpay_order(oid);

        if (json) {
            net_log("[NET] Parsing JSON...\n");
            const char *k = strstr(json, "\"qr_url\"");
            if (k) {
                k = strchr(k, ':');
                if (k) {
                    k++; while (*k == ' ') k++;
                    if (*k == '"') k++;
                    const char *end = strchr(k, '"');
                    int qlen = end ? (int)(end - k) : 0;
                    if (qlen > 0 && qlen < (int)sizeof(g_razorpay_url)) {
                        memcpy(g_razorpay_url, k, qlen);
                        g_razorpay_url[qlen] = '\0';
                    }
                }
            }
            free(json);
        }

        /* Now update LVGL widgets — acquire mutex */
        lvgl_acquire();
        if (g_razorpay_url[0] != '\0' && upi_qr_obj) {
            lv_qrcode_update(upi_qr_obj, g_razorpay_url, strlen(g_razorpay_url));
            if (upi_spinner) lv_obj_add_flag(upi_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_anim_t a; lv_anim_init(&a);
            lv_anim_set_var(&a, upi_qr_obj);
            lv_anim_set_values(&a, 0, 256);
            lv_anim_set_time(&a, 400);
            lv_anim_set_exec_cb(&a, anim_zoom_cb);
            lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
            lv_anim_start(&a);
            net_buzz(2);
        } else {
            /* QR failed — show fallback */
            if (upi_spinner) lv_obj_add_flag(upi_spinner, LV_OBJ_FLAG_HIDDEN);
            if (upi_qr_label) {
                lv_obj_clear_flag(upi_qr_label, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(upi_qr_label, "Staff Coming\nfor Payment\n\n(QR Unavailable)");
            }
            if (lbl_payment_result) {
                lv_label_set_text(lbl_payment_result, "Manual payment - staff notified");
                lv_obj_set_style_text_color(lbl_payment_result, COL_AMBER, 0);
            }
            net_buzz(1);
        }
        lvgl_release();
    }

    /* ---- Deferred UPI payment status poll (blocking HTTP) ---- */
    if (g_pending_upi_poll) {
        g_pending_upi_poll = false;
        int oid = sm_get_order_id();

        /* Network calls — NO mutex held */
        char status[NET_STATUS_LEN];
        net_get_payment_status(oid, status, sizeof(status));
        /* If still pending, also try razorpay-specific endpoint */
        if (strcmp(status, "pending") == 0) {
            net_get_razorpay_status(oid, status, sizeof(status));
        }

        /* Now update UI — acquire mutex */
        lvgl_acquire();
        if (strcmp(status, "paid") == 0 || strcmp(status, "verified") == 0) {
            safe_timer_del(&upi_poll_timer);
            if (lbl_payment_result) {
                lv_label_set_text(lbl_payment_result, "PAYMENT SUCCESSFUL " LV_SYMBOL_OK);
                lv_obj_set_style_text_color(lbl_payment_result, COL_SUCCESS, 0);
            }
            lv_timer_t *gt = lv_timer_create(upi_goto_feedback_cb, 2000, NULL);
            if (gt) lv_timer_set_repeat_count(gt, 1);
            lvgl_release();
            net_buzz(3);
        }
        else if (strcmp(status, "failed") == 0) {
            safe_timer_del(&upi_poll_timer);
            if (lbl_payment_result) {
                lv_label_set_text(lbl_payment_result, "Staff Coming for Payment");
                lv_obj_set_style_text_color(lbl_payment_result, COL_ERROR, 0);
            }
            lvgl_release();
            net_buzz(1);
        }
        else if (upi_timeout_count >= 100) {
            safe_timer_del(&upi_poll_timer);
            if (lbl_payment_result) {
                lv_label_set_text(lbl_payment_result, "Staff Coming for Payment");
                lv_obj_set_style_text_color(lbl_payment_result, COL_AMBER, 0);
            }
            lvgl_release();
            net_payment_timeout(oid);
            net_buzz(1);
        }
        else {
            lvgl_release();
        }
    }
}

void ui_set_wifi_connected(bool connected)
{
    g_wifi_ok = connected;
    if (!lbl_wifi_status) return;
    lv_label_set_text(lbl_wifi_status, connected ? "Connected" : "Offline");
    lv_obj_set_style_text_color(lbl_wifi_status,
        connected ? COL_SUCCESS : COL_ERROR, 0);
    /* Safely update the WiFi icon colour (first child of wifi_row) */
    if (lbl_wifi_status) {
        lv_obj_t *_parent = lv_obj_get_parent(lbl_wifi_status);
        if (_parent && lv_obj_get_child_cnt(_parent) > 0) {
            lv_obj_t *_icon = lv_obj_get_child(_parent, 0);
            if (_icon && _icon != lbl_wifi_status)
                lv_obj_set_style_text_color(_icon,
                    connected ? COL_SUCCESS : COL_ERROR, 0);
        }
    }
}

void ui_menu_load(const char *menu_json)
{
    if (!menu_json || !menu_grid) return;
    lv_obj_clean(menu_grid);
    char last_cat[64] = "";
    
    const char *p = menu_json;
    int parsed_count = 0;
    while ((p = strchr(p, '{')) != NULL) {
        const char *obj_end = find_obj_end(p);
        if (!obj_end) break;
        int id=0, price=0, is_veg=1, available=1;
        char name[64]="", desc[128]="", cat[64]="Main Course";
        const char *f;
        
        /* 1. Extract ID */
        f = strstr(p, "\"id\"");
        if (f && f < obj_end) {
            f = strchr(f, ':');
            if (f) { f++; while(*f == ' ') f++; id = atoi(f); }
        }

        /* 2. Extract Name */
        f = strstr(p, "\"name\"");
        if (f && f < obj_end) {
            f = strchr(f, ':');
            if (f) {
                f++; while(*f == ' ' || *f == '"') f++;
                const char *q = strchr(f, '"');
                if (q && q < obj_end) {
                    int l = (int)(q - f); if(l >= 64) l = 63;
                    memcpy(name, f, l); name[l] = 0;
                }
            }
        }

        /* 3. Extract Description */
        f = strstr(p, "\"description\"");
        if (f && f < obj_end) {
            f = strchr(f, ':');
            if (f) {
                f++; while(*f == ' ' || *f == '"') f++;
                const char *q = strchr(f, '"');
                if (q && q < obj_end) {
                    int l = (int)(q - f); if(l >= 128) l = 127;
                    memcpy(desc, f, l); desc[l] = 0;
                }
            }
        }

        /* 4. Extract Category */
        f = strstr(p, "\"category\"");
        if (f && f < obj_end) {
            f = strchr(f, ':');
            if (f) {
                f++; while(*f == ' ' || *f == '"') f++;
                const char *q = strchr(f, '"');
                if (q && q < obj_end) {
                    int l = (int)(q - f); if(l >= 64) l = 63;
                    memcpy(cat, f, l); cat[l] = 0;
                }
            }
        }

        /* 5. Extract Price */
        f = strstr(p, "\"price\"");
        if (f && f < obj_end) {
            f = strchr(f, ':');
            if (f) { f++; while(*f == ' ') f++; price = atoi(f); }
        }

        /* 6. Extract Boolean fields */
        f = strstr(p, "\"is_veg\"");
        if (f && f < obj_end) {
            f = strchr(f, ':');
            if (f) { f++; while(*f == ' ') f++; is_veg = (*f == 't' || *f == '1'); }
        }
        f = strstr(p, "\"available\"");
        if (f && f < obj_end) {
            f = strchr(f, ':');
            if (f) { f++; while(*f == ' ') f++; available = (*f == 't' || *f == '1'); }
        }

        if (id > 0 && name[0]) {
            if (strcmp(cat, last_cat) != 0) {
                make_category_header(cat);
                strncpy(last_cat, cat, sizeof(last_cat)-1);
            }
            add_menu_card(id, name, desc, cat, price*100, is_veg!=0, available!=0);
        parsed_count++;
        }
        p = obj_end + 1;
    }
}

void ui_menu_item_set_available(int item_id, bool available) { (void)item_id; (void)available; }
void ui_cart_refresh(void) { refresh_cart_panel(); }
void ui_order_set_wait_time(int minutes) { (void)minutes; }
void ui_food_ready_show(void) { sm_set(STATE_FOOD_READY); }
void ui_bill_load(const char *bill_json) { (void)bill_json; }

void ui_payment_set_amount(int paise)
{
    int r = paise / 100;
    if (lbl_upi_amount)  {
        char b[48]; snprintf(b,sizeof(b),"Amount Due: Rs. %d",r);
        lv_label_set_text(lbl_upi_amount, b);
    }
    if (lbl_cash_amount) {
        char b[16]; snprintf(b,sizeof(b),"Rs. %d",r);
        lv_label_set_text(lbl_cash_amount, b);
    }
}

void ui_upi_show_qr(const char *qr_url) { (void)qr_url; }

void ui_upi_show_result(bool success)
{
    if (lbl_payment_result) {
        lv_label_set_text(lbl_payment_result,
            success ? "Payment Verified " LV_SYMBOL_OK
                    : "Payment Failed "  LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(lbl_payment_result,
            success ? COL_SUCCESS : COL_ERROR, 0);
    }
}

void ui_feedback_set_countdown(int seconds)
{
    fb_seconds = seconds;
    if (lbl_fb_countdown) {
        char buf[32]; snprintf(buf,sizeof(buf),"Auto-submitting in %ds",seconds);
        lv_label_set_text(lbl_fb_countdown, buf);
    }
    if (feedback_bar) lv_bar_set_value(feedback_bar, seconds, LV_ANIM_OFF);
}

/* D2: declared in ui_screens.h */
void ui_add_menu_card(int id, const char *name, const char *desc,
                      int price_rupees, bool is_veg, bool available)
{
    add_menu_card(id, name, desc, "General", price_rupees * 100, is_veg, available);
}
