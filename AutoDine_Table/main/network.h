#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "menu_data.h"

// WiFi Functions
void network_wifi_init(void);
bool network_wifi_is_connected(void);

// API Functions
bool network_send_order(uint8_t table_id, cart_t *cart, bool append);
bool network_get_table_status(uint8_t table_id, char *response_buf, size_t buf_size);
bool network_request_bill(uint8_t table_id);
bool network_send_payment_method(uint8_t table_id, const char *method);

#endif
