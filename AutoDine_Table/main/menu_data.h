#ifndef MENU_DATA_H
#define MENU_DATA_H

#include <stdint.h>
#include <string.h> 
typedef struct {
    uint8_t item_id;
    char name[32];
    uint16_t price;
} menu_item_t;

typedef struct {
    uint8_t item_id;
    char name[32];
    uint16_t price;
    uint8_t quantity;
} cart_item_t;

typedef struct {
    cart_item_t items[20];
    uint8_t count;
    uint32_t total;
} cart_t;

#define MENU_ITEMS_COUNT 10

static const menu_item_t MENU_DATABASE[MENU_ITEMS_COUNT] = {
    {1, "Paneer Tikka", 250},
    {2, "Chicken Biryani", 300},
    {3, "Veg Biryani", 200},
    {4, "Dal Makhani", 180},
    {5, "Butter Naan", 50},
    {6, "Roti", 20},
    {7, "Masala Dosa", 120},
    {8, "Idli Sambar", 80},
    {9, "Chole Bhature", 150},
    {10, "Gulab Jamun", 60}
};

static inline void cart_init(cart_t *cart) {
    cart->count = 0;
    cart->total = 0;
}

static inline void cart_add_item(cart_t *cart, uint8_t item_id, const char *name, uint16_t price, uint8_t qty) {
    if (cart->count >= 20 || qty == 0) return;
    
    for (int i = 0; i < cart->count; i++) {
        if (cart->items[i].item_id == item_id) {
            cart->items[i].quantity += qty;
            cart->total += price * qty;
            return;
        }
    }
    
    cart->items[cart->count].item_id = item_id;
    strncpy(cart->items[cart->count].name, name, 31);
    cart->items[cart->count].name[31] = '\0';
    cart->items[cart->count].price = price;
    cart->items[cart->count].quantity = qty;
    cart->total += price * qty;
    cart->count++;
}

static inline bool cart_is_empty(const cart_t *cart) {
    return (cart->count == 0);
}

#endif
