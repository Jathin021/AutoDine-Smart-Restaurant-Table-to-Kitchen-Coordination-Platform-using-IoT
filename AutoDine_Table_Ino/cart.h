#pragma once
/* =====================================================================
 *  cart.h — AutoDine V4.0 in-memory cart
 * ===================================================================== */
#include <stdint.h>
#include <stdbool.h>

#define CART_MAX_ITEMS   32
#define CART_MAX_NAME    64
#define CART_MAX_DESC    128

typedef struct {
    int      id;
    char     name[CART_MAX_NAME];
    int      price_paise;   /* price in paise (₹ × 100) */
    int      qty;
    bool     is_veg;
} cart_item_t;

void cart_clear(void);
void cart_add(int id, const char *name, int price_paise, bool is_veg);
void cart_remove_one(int id);
int  cart_get_qty(int id);
int  cart_item_count(void);         /* unique items */
int  cart_total_items(void);        /* sum of quantities */
int  cart_subtotal_paise(void);
int  cart_gst_paise(void);          /* 5% GST */
int  cart_grand_total_paise(void);

/* Serialise cart to JSON string (caller must free) */
char *cart_to_json(void);

const cart_item_t *cart_get_items(void);
