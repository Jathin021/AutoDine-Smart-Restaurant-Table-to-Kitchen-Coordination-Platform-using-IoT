/* =====================================================================
 *  cart.c — AutoDine V4.0 in-memory shopping cart
 * ===================================================================== */
#include "cart.h"
#include "app_config.h"
#include "hardware_compat.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static cart_item_t s_items[CART_MAX_ITEMS];
static int         s_count = 0;

void cart_clear(void)
{
    memset(s_items, 0, sizeof(s_items));
    s_count = 0;
}

void cart_add(int id, const char *name, int price_paise, bool is_veg)
{
    /* Check if item already exists */
    for (int i = 0; i < s_count; i++) {
        if (s_items[i].id == id) {
            s_items[i].qty++;
            return;
        }
    }
    /* New item */
    if (s_count >= CART_MAX_ITEMS) {
        return;
    }
    s_items[s_count].id          = id;
    s_items[s_count].price_paise = price_paise;
    s_items[s_count].is_veg      = is_veg;
    s_items[s_count].qty         = 1;
    strncpy(s_items[s_count].name, name, CART_MAX_NAME - 1);
    s_items[s_count].name[CART_MAX_NAME - 1] = '\0';
    s_count++;
}

void cart_remove_one(int id)
{
    for (int i = 0; i < s_count; i++) {
        if (s_items[i].id == id) {
            s_items[i].qty--;
            if (s_items[i].qty <= 0) {
                /* Remove slot — shift left */
                memmove(&s_items[i], &s_items[i+1],
                        sizeof(cart_item_t) * (s_count - i - 1));
                s_count--;
            }
            return;
        }
    }
}

int cart_get_qty(int id)
{
    for (int i = 0; i < s_count; i++) {
        if (s_items[i].id == id) return s_items[i].qty;
    }
    return 0;
}

int cart_item_count(void)  { return s_count; }

int cart_total_items(void)
{
    int total = 0;
    for (int i = 0; i < s_count; i++) total += s_items[i].qty;
    return total;
}

int cart_subtotal_paise(void)
{
    int total = 0;
    for (int i = 0; i < s_count; i++) {
        total += s_items[i].price_paise * s_items[i].qty;
    }
    return total;
}

int cart_gst_paise(void)
{
    return cart_subtotal_paise() * 5 / 100;  /* 5% GST */
}

int cart_grand_total_paise(void)
{
    return cart_subtotal_paise() + cart_gst_paise();
}

char *cart_to_json(void)
{
    /* D3 Fix: Ensure buffer is large enough for all item names + JSON overhead.
     * Old: buf_size = 256 + s_count * 120  (could overflow with long names)
     * New: buf_size = 256 + s_count * (CART_MAX_NAME + 80)
     */
    size_t buf_size = 256 + (size_t)s_count * (CART_MAX_NAME + 80);
    char  *buf      = malloc(buf_size);
    if (!buf) return NULL;

    int offset = 0;
    offset += snprintf(buf + offset, buf_size - offset,
                       "{\"table\":%d,\"items\":[", TABLE_NUMBER);
    for (int i = 0; i < s_count; i++) {
        offset += snprintf(buf + offset, buf_size - offset,
                           "{\"id\":%d,\"name\":\"%s\",\"qty\":%d,\"price\":%d}%s",
                           s_items[i].id,
                           s_items[i].name,
                           s_items[i].qty,
                           s_items[i].price_paise / 100,
                           (i < s_count - 1) ? "," : "");
    }
    offset += snprintf(buf + offset, buf_size - offset,
                       "],\"subtotal\":%d,\"gst\":%d,\"total\":%d}",
                       cart_subtotal_paise() / 100,
                       cart_gst_paise() / 100,
                       cart_grand_total_paise() / 100);
    return buf;
}

const cart_item_t *cart_get_items(void) { return s_items; }
