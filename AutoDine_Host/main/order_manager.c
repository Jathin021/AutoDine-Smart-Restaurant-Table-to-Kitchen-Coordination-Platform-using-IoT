#include "order_manager.h"
#include "app_config.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ORDER_MGR";

static order_t orders[MAX_ORDERS];
static table_info_t tables[MAX_TABLES];
static uint8_t next_order_id = 1;

void order_manager_init(void) {
    memset(orders, 0, sizeof(orders));
    memset(tables, 0, sizeof(tables));
    
    tables[0].table_id = TABLE_1_ID;
    tables[0].status = TABLE_STATUS_IDLE;
    tables[0].order_state = ORDER_STATE_NONE;
    
    tables[1].table_id = TABLE_2_ID;
    tables[1].status = TABLE_STATUS_IDLE;
    tables[1].order_state = ORDER_STATE_NONE;
    
    ESP_LOGI(TAG, "Order manager initialized");
}
// ✅ REPLACE ENTIRE FUNCTION (line 31-85):
uint8_t order_manager_create_order(uint8_t table_id, cJSON *items_array, uint32_t total) {
    int table_idx = (table_id == TABLE_1_ID) ? 0 : 1;
    
    // ✅ FIX: Check if table already has an active order
    order_t *order = NULL;
    if (tables[table_idx].current_order_id > 0) {
        // Find existing order
        for (int i = 0; i < MAX_ORDERS; i++) {
            if (orders[i].active && orders[i].order_id == tables[table_idx].current_order_id) {
                order = &orders[i];
                break;
            }
        }
    }
    
    // If no existing order, create new one
    if (!order) {
        int free_slot = -1;
        for (int i = 0; i < MAX_ORDERS; i++) {
            if (!orders[i].active) {
                free_slot = i;
                break;
            }
        }
        if (free_slot == -1) {
            ESP_LOGE(TAG, "No free order slots");
            return 0;
        }
        
        order = &orders[free_slot];
        order->order_id = next_order_id++;
        order->table_id = table_id;
        order->total = 0;
        order->state = ORDER_STATE_PENDING;
        order->active = true;
        order->item_count = 0;
        
        tables[table_idx].current_order_id = order->order_id;
    }
    
    // ✅ FIX: Append items to existing order
    int item_count = cJSON_GetArraySize(items_array);
    for (int i = 0; i < item_count && order->item_count < MAX_ORDER_ITEMS; i++) {
        cJSON *item = cJSON_GetArrayItem(items_array, i);
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *price = cJSON_GetObjectItem(item, "price");
        cJSON *qty = cJSON_GetObjectItem(item, "qty");
        
        if (id && name && price && qty) {
            // Check if item already exists in order
            bool found = false;
            for (int j = 0; j < order->item_count; j++) {
                if (order->items[j].item_id == id->valueint) {
                    // Update quantity
                    order->items[j].quantity += qty->valueint;
                    found = true;
                    break;
                }
            }
            
            // Add new item if not found
            if (!found) {
                order->items[order->item_count].item_id = id->valueint;
                strncpy(order->items[order->item_count].name, name->valuestring, 31);
                order->items[order->item_count].name[31] = '\0';
                order->items[order->item_count].price = price->valueint;
                order->items[order->item_count].quantity = qty->valueint;
                order->item_count++;
            }
        }
    }
    
    // ✅ FIX: Update total (add new items' cost)
    order->total += total;
    tables[table_idx].order_state = ORDER_STATE_PENDING;
    
    ESP_LOGI(TAG, "Order %d updated for table %d: %d items, total=%lu",
        order->order_id, table_id, order->item_count, (unsigned long)order->total);
    
    return order->order_id;
}


bool order_manager_accept_order(uint8_t order_id) {
    order_t *order = NULL;
    for (int i = 0; i < MAX_ORDERS; i++) {
        if (orders[i].active && orders[i].order_id == order_id) {
            order = &orders[i];
            break;
        }
    }
    
    if (!order) return false;
    
    order->state = ORDER_STATE_ACCEPTED;
    
    int table_idx = (order->table_id == TABLE_1_ID) ? 0 : 1;
    tables[table_idx].status = TABLE_STATUS_COOKING;
    tables[table_idx].order_state = ORDER_STATE_ACCEPTED;
    
    ESP_LOGI(TAG, "Order %d accepted", order_id);
    return true;
}

bool order_manager_decline_order(uint8_t order_id) {
    order_t *order = NULL;
    for (int i = 0; i < MAX_ORDERS; i++) {
        if (orders[i].active && orders[i].order_id == order_id) {
            order = &orders[i];
            break;
        }
    }
    
    if (!order) return false;
    
    order->state = ORDER_STATE_DECLINED;
    order->active = false;
    
    int table_idx = (order->table_id == TABLE_1_ID) ? 0 : 1;
    tables[table_idx].status = TABLE_STATUS_IDLE;
    tables[table_idx].order_state = ORDER_STATE_DECLINED;
    tables[table_idx].current_order_id = 0;
    
    ESP_LOGI(TAG, "Order %d declined", order_id);
    return true;
}

bool order_manager_mark_prepared(uint8_t order_id) {
    order_t *order = NULL;
    for (int i = 0; i < MAX_ORDERS; i++) {
        if (orders[i].active && orders[i].order_id == order_id) {
            order = &orders[i];
            break;
        }
    }
    
    if (!order) return false;
    
    order->state = ORDER_STATE_PREPARED;
    
    int table_idx = (order->table_id == TABLE_1_ID) ? 0 : 1;
    tables[table_idx].status = TABLE_STATUS_PREPARED;
    tables[table_idx].order_state = ORDER_STATE_PREPARED;
    
    ESP_LOGI(TAG, "Order %d marked as prepared", order_id);
    return true;
}

bool order_manager_generate_bill(uint8_t table_id) {
    int table_idx = (table_id == TABLE_1_ID) ? 0 : 1;
    
    order_t *order = NULL;
    for (int i = 0; i < MAX_ORDERS; i++) {
        if (orders[i].active && orders[i].table_id == table_id) {
            order = &orders[i];
            break;
        }
    }
    
    if (!order) return false;
    
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    
    for (int i = 0; i < order->item_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", order->items[i].name);
        cJSON_AddNumberToObject(item, "qty", order->items[i].quantity);
        cJSON_AddNumberToObject(item, "price", order->items[i].price);
        cJSON_AddNumberToObject(item, "subtotal", 
                               order->items[i].price * order->items[i].quantity);
        cJSON_AddItemToArray(items, item);
    }
    
    cJSON_AddItemToObject(root, "items", items);
    
    uint32_t subtotal = order->total;
    double gst = subtotal * 0.18;
    double grand_total = subtotal + gst;
    
    cJSON_AddNumberToObject(root, "subtotal", subtotal);
    cJSON_AddNumberToObject(root, "gst", gst);
    cJSON_AddNumberToObject(root, "grand_total", grand_total);
    
    char *json_str = cJSON_PrintUnformatted(root);
    strncpy(tables[table_idx].bill_json, json_str, sizeof(tables[table_idx].bill_json) - 1);
    tables[table_idx].bill_json[sizeof(tables[table_idx].bill_json) - 1] = '\0';
    
    free(json_str);
    cJSON_Delete(root);
    
    tables[table_idx].status = TABLE_STATUS_BILLING;
    
    ESP_LOGI(TAG, "Bill generated for table %d: subtotal=%lu, gst=%.2f, total=%.2f",
             table_id, (unsigned long)subtotal, gst, grand_total);
    
    return true;
}

bool order_manager_set_payment_method(uint8_t table_id, const char *method) {
    int table_idx = (table_id == TABLE_1_ID) ? 0 : 1;
    
    strncpy(tables[table_idx].payment_method, method, 15);
    tables[table_idx].payment_method[15] = '\0';
    tables[table_idx].status = TABLE_STATUS_PAYMENT_WAITING;
    
    ESP_LOGI(TAG, "Payment method set for table %d: %s", table_id, method);
    return true;
}

bool order_manager_verify_payment(uint8_t table_id) {
    int table_idx = (table_id == TABLE_1_ID) ? 0 : 1;
    
    for (int i = 0; i < MAX_ORDERS; i++) {
        if (orders[i].active && orders[i].table_id == table_id) {
            orders[i].active = false;
        }
    }
    
    tables[table_idx].status = TABLE_STATUS_IDLE;
    tables[table_idx].order_state = ORDER_STATE_NONE;
    tables[table_idx].current_order_id = 0;
    memset(tables[table_idx].bill_json, 0, sizeof(tables[table_idx].bill_json));
    memset(tables[table_idx].payment_method, 0, sizeof(tables[table_idx].payment_method));
    
    ESP_LOGI(TAG, "Payment verified for table %d, table reset", table_id);
    return true;
}

table_info_t* order_manager_get_table_info(uint8_t table_id) {
    int table_idx = (table_id == TABLE_1_ID) ? 0 : 1;
    return &tables[table_idx];
}

order_t* order_manager_get_order(uint8_t order_id) {
    for (int i = 0; i < MAX_ORDERS; i++) {
        if (orders[i].active && orders[i].order_id == order_id) {
            return &orders[i];
        }
    }
    return NULL;
}

void order_manager_get_all_tables_json(char *buffer, size_t buf_size) {
    cJSON *root = cJSON_CreateArray();
    
    for (int i = 0; i < MAX_TABLES; i++) {
        cJSON *table = cJSON_CreateObject();
        cJSON_AddNumberToObject(table, "table_id", tables[i].table_id);
        cJSON_AddStringToObject(table, "status", 
            tables[i].status == TABLE_STATUS_IDLE ? "idle" :
            tables[i].status == TABLE_STATUS_COOKING ? "cooking" :
            tables[i].status == TABLE_STATUS_PREPARED ? "prepared" :
            tables[i].status == TABLE_STATUS_BILLING ? "billing" : "payment");
        cJSON_AddStringToObject(table, "order_state",
            tables[i].order_state == ORDER_STATE_NONE ? "none" :
            tables[i].order_state == ORDER_STATE_PENDING ? "pending" :
            tables[i].order_state == ORDER_STATE_ACCEPTED ? "accepted" :
            tables[i].order_state == ORDER_STATE_DECLINED ? "declined" : "prepared");
        cJSON_AddNumberToObject(table, "order_id", tables[i].current_order_id);
        
        if (tables[i].current_order_id > 0) {
            order_t *order = order_manager_get_order(tables[i].current_order_id);
            if (order) {
                cJSON *items = cJSON_CreateArray();
                for (int j = 0; j < order->item_count; j++) {
                    cJSON *item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "name", order->items[j].name);
                    cJSON_AddNumberToObject(item, "qty", order->items[j].quantity);
                    cJSON_AddNumberToObject(item, "price", order->items[j].price);
                    cJSON_AddItemToArray(items, item);
                }
                cJSON_AddItemToObject(table, "items", items);
                cJSON_AddNumberToObject(table, "total", order->total);
            }
        }
        
        if (strlen(tables[i].payment_method) > 0) {
            cJSON_AddStringToObject(table, "payment_method", tables[i].payment_method);
        }
        
        cJSON_AddItemToArray(root, table);
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    strncpy(buffer, json_str, buf_size - 1);
    buffer[buf_size - 1] = '\0';
    
    free(json_str);
    cJSON_Delete(root);
}
