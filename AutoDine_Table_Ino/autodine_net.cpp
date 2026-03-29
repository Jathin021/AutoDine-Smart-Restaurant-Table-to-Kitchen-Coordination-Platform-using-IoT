/* autodine_net.cpp — AutoDine V4.0 HTTP client (Arduino HTTPClient)
 * All routes aligned to actual Flask server (server.py) endpoints.
 *
 * NOTE (D4): A static HTTPClient instance is reused across all calls.
 * This is safe because every request targets the same SERVER_BASE_URL host.
 * http.end() safety rule: call http.end() if still connected BEFORE http.begin()
 * to prevent connection-pool leaks in the ESP32 Arduino core.
 *
 * NEW FUNCTIONS ADDED (V4.0 Bug Fixes):
 *   net_append_order()   — POST /api/order/append (BUG 1 Fix: add-more-items)
 *   net_payment_timeout()— POST /api/payment/timeout (Razorpay QR timeout)
 */
#include "autodine_net.h"
#include "app_config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

static HTTPClient http;

/* ═══════════════════════════════════════════════════════════════════
 *  Safety helper — always call before http.begin() to avoid leaks
 * ═══════════════════════════════════════════════════════════════════ */
static inline void http_safe_end(void)
{
    if (http.connected()) http.end();
}

/* ═══════════════════════════════════════════════════════════════════
 *  Bug 10 — WiFi health check callable from C (state_machine.c)
 * ═══════════════════════════════════════════════════════════════════ */
bool net_is_wifi_ok(void)
{
    return WiFi.status() == WL_CONNECTED;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Bug 11 — JSON string escaper for user-supplied comment text
 * ═══════════════════════════════════════════════════════════════════ */
static void json_escape(const char *src, char *dst, int dst_len)
{
    int di = 0;
    for (int i = 0; src[i] && di < dst_len - 3; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            dst[di++] = '\\';
            dst[di++] = c;
        } else if (c == '\n') {
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else if (c == '\r') {
            /* skip carriage returns */
        } else if (c == '\t') {
            dst[di++] = '\\';
            dst[di++] = 't';
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

/* ── Low-level helpers ─────────────────────────────────────────────── */
static char *http_get(const char *url)
{
    if (WiFi.status() != WL_CONNECTED) return NULL;
    Serial.printf("[HTTP] GET: %s\n", url);
    http_safe_end();                          /* safety: end before begin */
    http.begin(url);
    http.setTimeout(NET_TIMEOUT_MS);
    int code = http.GET();
    Serial.printf("[HTTP] Code: %d\n", code);
    if (code != 200) { http.end(); return NULL; }
    String body = http.getString();
    http.end();
    char *buf = (char *)malloc(body.length() + 1);
    if (!buf) return NULL;
    memcpy(buf, body.c_str(), body.length() + 1);
    return buf;
}

static String http_post_str(const char *url, const char *body_json)
{
    if (WiFi.status() != WL_CONNECTED) return "";
    Serial.printf("[HTTP] POST: %s\n", url);
    http_safe_end();                          /* safety */
    http.begin(url);
    http.setTimeout(NET_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)body_json, strlen(body_json));
    Serial.printf("[HTTP] Code: %d\n", code);
    String resp = (code == 200 || code == 201) ? http.getString() : "";
    http.end();
    return resp;
}

static int http_post(const char *url, const char *body_json)
{
    if (WiFi.status() != WL_CONNECTED) return -1;
    http_safe_end();                          /* safety */
    http.begin(url);
    http.setTimeout(NET_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)body_json, strlen(body_json));
    http.end();
    return code;
}

/* ── Menu ──  GET /api/menu ─────────────────────────────────────────── */
char *net_fetch_menu(void)
{
    return http_get(SERVER_BASE_URL "/api/menu");
}

/* ── Orders ─────────────────────────────────────────────────────────── */

/* POST /api/order  body: {"table":N,"items":[...]}
 * cart_json must already be the full body with "table" and "items" keys.
 * Returns 0 on success, fills *out_order_id.
 */
int net_place_order(const char *cart_json, int *out_order_id)
{
    if (WiFi.status() != WL_CONNECTED) return -1;
    String resp = http_post_str(SERVER_BASE_URL "/api/order", cart_json);
    if (resp.length() == 0) return -1;
    /* Parse {"ok":true,"order_id":N} with flexible spacing */
    const char *p = strstr(resp.c_str(), "\"order_id\"");
    if (p && out_order_id) {
        p = strchr(p, ':');
        if (p) { p++; while(*p == ' ') p++; *out_order_id = atoi(p); }
    }
    Serial.printf("[ORDER] Placed! order_id = %d\n", out_order_id ? *out_order_id : -1);
    return 0;
}

/* BUG 1 FIX: Append new items to existing order.
 * POST /api/order/append  body: {"order_id":X,"items":[...]}
 * Server does NOT create a new order — it appends items to the existing one.
 * Returns 0 on success, -1 on failure.
 */
int net_append_order(int order_id, const char *new_items_json)
{
    if (WiFi.status() != WL_CONNECTED) return -1;
    /* new_items_json is the raw items JSON array string, e.g. [{"id":1,...}]
     * We wrap it into: {"order_id":X,"items":[...]}
     * Maximum body size: 4096 bytes should be sufficient for any realistic cart */
    char body[4096];
    snprintf(body, sizeof(body),
             "{\"order_id\":%d,\"items\":%s}",
             order_id, new_items_json ? new_items_json : "[]");
    String resp = http_post_str(SERVER_BASE_URL "/api/order/append", body);
    if (resp.length() == 0) return -1;
    /* Server returns same order_id: {"ok":true,"order_id":X} */
    return 0;
}

/* GET /api/order/status?order_id=N */
void net_get_order_status(int order_id, char *out_buf, int buf_len)
{
    char url[160];
    snprintf(url, sizeof(url), SERVER_BASE_URL "/api/order/status?order_id=%d", order_id);
    char *resp = http_get(url);
    if (resp) {
        /* Flexible parse: find "status", then colon, then skip spaces and opening quote */
        const char *p = strstr(resp, "\"status\"");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                p++; while(*p == ' ') p++;  /* skip spaces */
                if (*p == '"') p++;          /* skip opening quote */
                const char *e = strchr(p, '"');
                int len = e ? (int)(e - p) : 0;
                if (len >= buf_len) len = buf_len - 1;
                if (len > 0) {
                    memcpy(out_buf, p, len);
                    out_buf[len] = '\0';
                } else strncpy(out_buf, "error", buf_len);
            } else strncpy(out_buf, "error", buf_len);
        } else strncpy(out_buf, "error", buf_len);
        Serial.printf("[POLL] Order #%d status = '%s'\n", order_id, out_buf);
        free(resp);
    } else {
        strncpy(out_buf, "error", buf_len);
        Serial.printf("[POLL] Order #%d - no response\n", order_id);
    }
}

/* POST /api/order/food-served  body: {"order_id":N} */
int net_food_served(int order_id)
{
    char body[48];
    snprintf(body, sizeof(body), "{\"order_id\":%d}", order_id);
    return http_post(SERVER_BASE_URL "/api/order/food-served", body) == 200 ? 0 : -1;
}

/* POST /api/buzz  (waiter call = pattern 1) */
int net_call_waiter(int order_id)
{
    (void)order_id;
    return http_post(SERVER_BASE_URL "/api/buzz",
                     "{\"pattern\":1}") == 200 ? 0 : -1;
}

/* ── Bill ── POST /api/order/bill  body: {"order_id":N} ────────────── */
char *net_request_bill(int order_id)
{
    char body[48];
    snprintf(body, sizeof(body), "{\"order_id\":%d}", order_id);
    if (WiFi.status() != WL_CONNECTED) return NULL;
    http_safe_end();                          /* safety */
    http.begin(SERVER_BASE_URL "/api/order/bill");
    http.setTimeout(NET_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)body, strlen(body));
    if (code != 200) { http.end(); return NULL; }
    String s = http.getString();
    http.end();
    char *buf = (char *)malloc(s.length() + 1);
    if (buf) memcpy(buf, s.c_str(), s.length() + 1);
    return buf;
}

/* ── Payment ─────────────────────────────────────────────────────────── */

/* POST /api/payment/method  body: {"order_id":N,"method":"..."} */
int net_select_payment(int order_id, const char *method)
{
    char body[96];
    snprintf(body, sizeof(body),
             "{\"order_id\":%d,\"method\":\"%s\"}", order_id, method);
    return http_post(SERVER_BASE_URL "/api/payment/method", body) == 200 ? 0 : -1;
}

/* GET /api/payment/status?order_id=N */
void net_get_payment_status(int order_id, char *out_buf, int buf_len)
{
    char url[160];
    snprintf(url, sizeof(url),
             SERVER_BASE_URL "/api/payment/status?order_id=%d", order_id);
    char *resp = http_get(url);
    if (resp) {
        const char *p = strstr(resp, "\"status\"");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                p++; while(*p == ' ') p++;
                if (*p == '"') p++;
                const char *e = strchr(p, '"');
                int len = e ? (int)(e - p) : 0;
                if (len >= buf_len) len = buf_len - 1;
                if (len > 0) {
                    memcpy(out_buf, p, len);
                    out_buf[len] = '\0';
                } else strncpy(out_buf, "error", buf_len);
            } else strncpy(out_buf, "error", buf_len);
        } else strncpy(out_buf, "error", buf_len);
        Serial.printf("[POLL] Payment #%d status = '%s'\n", order_id, out_buf);
        free(resp);
    } else {
        strncpy(out_buf, "error", buf_len);
    }
}

/* POST /api/razorpay/create-order  body: {"order_id":N}
 * Returns malloc'd JSON string with qr_url, amount_paise, plink_id.
 * Caller must free().
 */
char *net_create_razorpay_order(int order_id)
{
    char body[48];
    snprintf(body, sizeof(body), "{\"order_id\":%d}", order_id);
    if (WiFi.status() != WL_CONNECTED) return NULL;
    http_safe_end();                          /* safety */
    http.begin(SERVER_BASE_URL "/api/razorpay/create-order");
    http.setTimeout(NET_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)body, strlen(body));
    if (code != 200) { http.end(); return NULL; }
    String s = http.getString();
    http.end();
    char *buf = (char *)malloc(s.length() + 1);
    if (buf) memcpy(buf, s.c_str(), s.length() + 1);
    return buf;
}

/* GET /api/razorpay/status/<order_id>
 * Polls Razorpay Payment Link status.
 * Writes "paid", "pending", or "error" into out_buf. */
void net_get_razorpay_status(int order_id, char *out_buf, int buf_len)
{
    char url[160];
    snprintf(url, sizeof(url), SERVER_BASE_URL "/api/razorpay/status/%d", order_id);
    char *resp = http_get(url);
    if (resp) {
        const char *p = strstr(resp, "\"status\"");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                p++; while(*p == ' ') p++;
                if (*p == '"') p++;
                const char *e = strchr(p, '"');
                int len = e ? (int)(e - p) : 0;
                if (len >= buf_len) len = buf_len - 1;
                if (len > 0) {
                    memcpy(out_buf, p, len);
                    out_buf[len] = '\0';
                } else strncpy(out_buf, "error", buf_len);
            } else strncpy(out_buf, "error", buf_len);
        } else strncpy(out_buf, "error", buf_len);
        free(resp);
    } else strncpy(out_buf, "error", buf_len);
}

/* NEW: Returns the FULL JSON response from /api/order/status?order_id=N */
int net_get_order_json(int order_id, char *out_buf, int buf_len)
{
    char url[160];
    snprintf(url, sizeof(url), SERVER_BASE_URL "/api/order/status?order_id=%d", order_id);
    char *resp = http_get(url);
    if (!resp) return -1;
    
    strncpy(out_buf, resp, buf_len - 1);
    out_buf[buf_len - 1] = '\0';
    free(resp);
    return 0;
}

/* NEW: POST /api/payment/timeout  body: {"order_id":N}
 * Called when 5-minute UPI countdown expires without payment.
 * Returns 0 on success, -1 on failure.
 */
int net_payment_timeout(int order_id)
{
    char body[48];
    snprintf(body, sizeof(body), "{\"order_id\":%d}", order_id);
    return http_post(SERVER_BASE_URL "/api/payment/timeout", body) == 200 ? 0 : -1;
}

/* ── Feedback ─ POST /api/feedback ──────────────────────────────────── */
int net_submit_feedback(int order_id, int stars, const char *comment)
{
    /* Bug 11 Fix: escape user-supplied comment to prevent JSON injection */
    char escaped[512];
    json_escape(comment ? comment : "", escaped, sizeof(escaped));

    char body[640];
    snprintf(body, sizeof(body),
             "{\"order_id\":%d,\"stars\":%d,\"comment\":\"%s\"}",
             order_id, stars, escaped);
    return http_post(SERVER_BASE_URL "/api/feedback", body) == 200 ? 0 : -1;
}

/* ── Buzz ─ POST /api/buzz ───────────────────────────────────────────── */
int net_buzz(int pattern)
{
    char body[32];
    snprintf(body, sizeof(body), "{\"pattern\":%d}", pattern);
    return http_post(SERVER_BASE_URL "/api/buzz", body) == 200 ? 0 : -1;
}

/* ── Availability ─ GET /api/menu/availability ───────────────────────── */
char *net_get_availability(void)
{
    return http_get(SERVER_BASE_URL "/api/menu/availability");
}

/* Logging bridge for C files */
void net_log(const char *fmt, ...)
{
    static char log_buf[256]; /* static to save stack */
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_buf, sizeof(log_buf), fmt, args);
    va_end(args);
    Serial.print(log_buf);
}

void net_delay(uint32_t ms)
{
    delay(ms);
}
