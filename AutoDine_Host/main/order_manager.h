#ifndef ORDER_MANAGER_H
#define ORDER_MANAGER_H

#include "app_config.h"  


#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"

typedef enum {
    TABLE_STATUS_IDLE = 0,
    TABLE_STATUS_COOKING,
    TABLE_STATUS_PREPARED,
    TABLE_STATUS_BILLING,
    TABLE_STATUS_PAYMENT_WAITING
} table_status_t;

typedef enum {
    ORDER_STATE_NONE = 0,
    ORDER_STATE_PENDING,
    ORDER_STATE_ACCEPTED,
    ORDER_STATE_DECLINED,
    ORDER_STATE_PREPARED
} order_state_t;

typedef struct {
    uint8_t item_id;
    char name[32];
    uint16_t price;
    uint8_t quantity;
} order_item_t;

typedef struct {
    uint8_t order_id;
    uint8_t table_id;
    order_item_t items[MAX_ORDER_ITEMS];
    uint8_t item_count;
    uint32_t total;
    order_state_t state;
    bool active;
} order_t;

typedef struct {
    uint8_t table_id;
    table_status_t status;
    order_state_t order_state;
    uint8_t current_order_id;
    char bill_json[2048];
    char payment_method[16];
} table_info_t;

void order_manager_init(void);
uint8_t order_manager_create_order(uint8_t table_id, cJSON *items_array, uint32_t total);
bool order_manager_accept_order(uint8_t order_id);
bool order_manager_decline_order(uint8_t order_id);
bool order_manager_mark_prepared(uint8_t order_id);
bool order_manager_generate_bill(uint8_t table_id);
bool order_manager_set_payment_method(uint8_t table_id, const char *method);
bool order_manager_verify_payment(uint8_t table_id);
table_info_t* order_manager_get_table_info(uint8_t table_id);
order_t* order_manager_get_order(uint8_t order_id);
void order_manager_get_all_tables_json(char *buffer, size_t buf_size);

#endif
