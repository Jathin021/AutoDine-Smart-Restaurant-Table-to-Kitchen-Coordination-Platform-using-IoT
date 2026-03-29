#pragma once
/* autodine_net.h — AutoDine V4.0 HTTP client (Arduino version) */
#include <stdint.h>
#include <stdbool.h>

#define NET_STATUS_LEN 32

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi health check (Bug 10: allows state_machine to drive wifi indicator) */
bool net_is_wifi_ok(void);

/* Menu */
char *net_fetch_menu(void);

/* Orders */
int  net_place_order(const char *cart_json, int *out_order_id);
int  net_food_served(int order_id);
int  net_call_waiter(int order_id);
void net_get_order_status(int order_id, char *out_buf, int buf_len);
int  net_get_order_json(int order_id, char *out_buf, int buf_len);

/* BUG 1 FIX: Append items to an existing order (append_mode flow) */
/* POST /api/order/append  body: {"order_id":X,"items":[...]} */
int  net_append_order(int order_id, const char *new_items_json);

/* Bill */
char *net_request_bill(int order_id);

/* Payment */
int   net_select_payment(int order_id, const char *method);
void  net_get_payment_status(int order_id, char *out_buf, int buf_len);
char *net_create_razorpay_order(int order_id);

/* Poll Razorpay payment link status: GET /api/razorpay/status/<order_id> */
void  net_get_razorpay_status(int order_id, char *out_buf, int buf_len);

/* NEW: report payment timeout to server */
int   net_payment_timeout(int order_id);

/* Logging / Delay wrappers (Bridging C and C++ Arduino funciones) */
void net_log(const char *fmt, ...);
void net_delay(uint32_t ms);

/* Feedback */
int net_submit_feedback(int order_id, int stars, const char *comment);

/* Buzzer */
int net_buzz(int pattern);

/* Availability */
char *net_get_availability(void);

#ifdef __cplusplus
}
#endif
