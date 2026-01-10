#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"
#define TAG "HOST_SERVER"

/* WiFi Configuration */
#define WIFI_SSID      "Restaurant_WiFi"
#define WIFI_PASS      "restaurant123"
#define WIFI_CHANNEL   1
#define MAX_CONNECTIONS 4

/* Order Status */
typedef enum {
    ORDER_PENDING = 0,
    ORDER_ACCEPTED = 1,
    ORDER_REJECTED = 2,
    ORDER_PREPARING = 3,
    ORDER_READY = 4,
    ORDER_COMPLETED = 5
} order_status_t;

/* Order Storage */
typedef struct {
    int id;
    int table_number;
    int section_id;
    char items[10][50];
    int prices[10];
    int item_count;
    int total_price;
    order_status_t status;
    uint32_t timestamp;
} order_t;

/* Bill Structure */
typedef struct {
    int section_id;
    int table_number;
    char items[50][50];
    int prices[50];
    int item_count;
    int subtotal;
    int gst;
    int total;
    bool generated;
    bool bill_requested;      // NEW: Customer requested bill
    bool payment_verified;
    char payment_method[20];
} bill_t;

#define MAX_ORDERS 50
#define MAX_BILLS 20

static order_t orders[MAX_ORDERS];
static bill_t bills[MAX_BILLS];
static int order_count = 0;
static int bill_count = 0;
static int next_order_id = 1;
static httpd_handle_t server = NULL;

/* ==================== ORDER MANAGEMENT ==================== */
static int create_order(int table, int section, cJSON *items_array, cJSON *prices_array)
{
    if (order_count >= MAX_ORDERS) {
        ESP_LOGW(TAG, "Order buffer full");
        return -1;
    }
    
    int count = cJSON_GetArraySize(items_array);
    if (count == 0 || count > 10) {
        ESP_LOGE(TAG, "Invalid item count: %d", count);
        return -1;
    }
    
    orders[order_count].id = next_order_id++;
    orders[order_count].table_number = table;
    orders[order_count].section_id = section;
    orders[order_count].item_count = count;
    orders[order_count].total_price = 0;
    orders[order_count].status = ORDER_PENDING;
    orders[order_count].timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(items_array, i);
        cJSON *price = cJSON_GetArrayItem(prices_array, i);
        
        if (cJSON_IsString(item) && cJSON_IsNumber(price)) {
            strncpy(orders[order_count].items[i], item->valuestring, sizeof(orders[order_count].items[i]) - 1);
            orders[order_count].items[i][sizeof(orders[order_count].items[i]) - 1] = '\0';
            orders[order_count].prices[i] = price->valueint;
            orders[order_count].total_price += price->valueint;
        }
    }
    
    int order_id = orders[order_count].id;
    order_count++;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "NEW ORDER #%d", order_id);
    ESP_LOGI(TAG, "Table: %d | Section: %d", table, section);
    ESP_LOGI(TAG, "Items: %d | Total: Rs.%d", count, orders[order_count-1].total_price);
    ESP_LOGI(TAG, "========================================");
    
    return order_id;
}

static bool update_order_status(int order_id, order_status_t new_status)
{
    for (int i = 0; i < order_count; i++) {
        if (orders[i].id == order_id) {
            orders[i].status = new_status;
            ESP_LOGI(TAG, "Order #%d → Status: %d", order_id, new_status);
            return true;
        }
    }
    return false;
}

static order_t* get_order_by_id(int order_id)
{
    for (int i = 0; i < order_count; i++) {
        if (orders[i].id == order_id) {
            return &orders[i];
        }
    }
    return NULL;
}

/* ==================== BILL MANAGEMENT ==================== */

// NEW: Request bill (customer double-pressed Button-1)
static bool request_bill(int section_id)
{
    // Check if bill request already exists
    for (int i = 0; i < bill_count; i++) {
        if (bills[i].section_id == section_id) {
            if (!bills[i].bill_requested) {
                bills[i].bill_requested = true;
                ESP_LOGI(TAG, "Bill requested for section %d", section_id);
                return true;
            }
            return false; // Already requested
        }
    }
    
    // Create new bill request entry
    if (bill_count >= MAX_BILLS) {
        ESP_LOGW(TAG, "Bill buffer full");
        return false;
    }
    
    bills[bill_count].section_id = section_id;
    bills[bill_count].bill_requested = true;
    bills[bill_count].generated = false;
    bills[bill_count].payment_verified = false;
    bills[bill_count].item_count = 0;
    strcpy(bills[bill_count].payment_method, "pending");
    
    // Get table number from orders
    for (int i = 0; i < order_count; i++) {
        if (orders[i].section_id == section_id && 
            (orders[i].status == ORDER_READY || orders[i].status == ORDER_ACCEPTED)) {
            bills[bill_count].table_number = orders[i].table_number;
            break;
        }
    }
    
    bill_count++;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "BILL REQUESTED - Section %d", section_id);
    ESP_LOGI(TAG, "Waiting for chef approval...");
    ESP_LOGI(TAG, "========================================");
    
    return true;
}

// Generate bill (chef clicked "Generate Bill")
static int generate_bill(int section_id)
{
    bill_t *bill = NULL;
    
    // Find existing bill request
    for (int i = 0; i < bill_count; i++) {
        if (bills[i].section_id == section_id) {
            bill = &bills[i];
            break;
        }
    }
    
    if (bill == NULL) {
        ESP_LOGW(TAG, "No bill request for section %d", section_id);
        return -1;
    }
    
    if (bill->generated) {
        ESP_LOGI(TAG, "Bill already generated for section %d", section_id);
        return section_id;
    }
    
    // Calculate bill from orders
    bill->subtotal = 0;
    bill->item_count = 0;
    
    for (int i = 0; i < order_count; i++) {
        if (orders[i].section_id == section_id && 
            (orders[i].status == ORDER_READY || orders[i].status == ORDER_ACCEPTED || orders[i].status == ORDER_PREPARING)) {
            
            bill->table_number = orders[i].table_number;
            
            for (int j = 0; j < orders[i].item_count; j++) {
                if (bill->item_count < 50) {
                    strncpy(bill->items[bill->item_count], orders[i].items[j], sizeof(bill->items[0]) - 1);
                    bill->items[bill->item_count][sizeof(bill->items[0]) - 1] = '\0';
                    bill->prices[bill->item_count] = orders[i].prices[j];
                    bill->subtotal += orders[i].prices[j];
                    bill->item_count++;
                }
            }
        }
    }
    
    if (bill->item_count == 0) {
        ESP_LOGW(TAG, "No items for bill section %d", section_id);
        return -1;
    }
    
    bill->gst = (bill->subtotal * 18) / 100;
    bill->total = bill->subtotal + bill->gst;
    bill->generated = true;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "BILL GENERATED - Section %d", section_id);
    ESP_LOGI(TAG, "Items: %d | Subtotal: Rs.%d", bill->item_count, bill->subtotal);
    ESP_LOGI(TAG, "GST: Rs.%d | Total: Rs.%d", bill->gst, bill->total);
    ESP_LOGI(TAG, "========================================");
    
    return section_id;
}

static bill_t* get_bill_by_section(int section_id)
{
    for (int i = 0; i < bill_count; i++) {
        if (bills[i].section_id == section_id) {
            return &bills[i];
        }
    }
    return NULL;
}

static bool update_payment_method(int section_id, const char *method)
{
    bill_t *bill = get_bill_by_section(section_id);
    if (bill != NULL && bill->generated) {
        strncpy(bill->payment_method, method, sizeof(bill->payment_method) - 1);
        bill->payment_method[sizeof(bill->payment_method) - 1] = '\0';
        ESP_LOGI(TAG, "Payment method updated - Section %d: %s", section_id, method);
        return true;
    }
    return false;
}

static bool verify_payment(int section_id)
{
    bill_t *bill = get_bill_by_section(section_id);
    if (bill == NULL || !bill->generated) {
        ESP_LOGW(TAG, "Bill not found or not generated for section %d", section_id);
        return false;
    }
    
    bill->payment_verified = true;
    
    // Mark all orders as completed
    for (int i = 0; i < order_count; i++) {
        if (orders[i].section_id == section_id) {
            orders[i].status = ORDER_COMPLETED;
        }
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "PAYMENT VERIFIED - Section %d", section_id);
    ESP_LOGI(TAG, "Method: %s | Amount: Rs.%d", bill->payment_method, bill->total);
    ESP_LOGI(TAG, "========================================");
    
    return true;
}

/* ==================== WEB PAGE HTML ==================== */
/* ==================== WEB PAGE HTML ==================== */
static void build_webpage(char *html, size_t max_size)
{
    int pos = snprintf(html, max_size,
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Chef Dashboard</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#667eea 0%%,#764ba2 100%%);min-height:100vh;padding:20px}"
        "h1{background:linear-gradient(135deg,#FF6B6B,#FF5722);color:white;padding:25px;border-radius:15px;text-align:center;margin:0 0 30px 0;box-shadow:0 8px 20px rgba(255,87,34,0.3);font-size:32px;letter-spacing:1px}"
        ".container{max-width:1400px;margin:0 auto}"
        ".order-card{border:none;padding:25px;margin:20px 0;background:white;border-radius:15px;box-shadow:0 10px 30px rgba(0,0,0,0.15);transition:all 0.3s ease;border-left:6px solid #FF5722}"
        ".order-card:hover{transform:translateY(-5px);box-shadow:0 15px 40px rgba(0,0,0,0.25)}"
        ".order-card.accepted{border-left-color:#4CAF50;background:linear-gradient(to right,#f0fff4 0%%,#ffffff 100%%)}"
        ".order-card.ready{border-left-color:#9C27B0;background:linear-gradient(to right,#f3e5f5 0%%,#ffffff 100%%)}"
        ".order-card.cancelled{border-left-color:#999;background:#f9f9f9;opacity:0.7}"
        ".bill-card{border:none;padding:30px;margin:30px 0;background:linear-gradient(135deg,#e3f2fd 0%%,#ffffff 100%%);border-radius:20px;box-shadow:0 12px 35px rgba(33,150,243,0.3);border-left:8px solid #2196F3}"
        ".bill-request-card{border:none;padding:30px;margin:30px 0;background:linear-gradient(135deg,#FFF8E1 0%%,#FFFDE7 100%%);border-radius:20px;box-shadow:0 12px 35px rgba(255,193,7,0.4);animation:pulse 2s infinite;border-left:8px solid #FFC107}"
        "@keyframes pulse{0%%,100%%{transform:scale(1);box-shadow:0 12px 35px rgba(255,193,7,0.4)}50%%{transform:scale(1.02);box-shadow:0 15px 45px rgba(255,193,7,0.6)}}"
        ".order-header{display:flex;justify-content:space-between;margin-bottom:20px;align-items:center;flex-wrap:wrap;gap:10px}"
        ".order-id{font-size:28px;font-weight:700;color:#FF5722;text-shadow:1px 1px 2px rgba(0,0,0,0.1)}"
        ".table-badge{background:linear-gradient(135deg,#FF5722,#FF6B6B);color:white;padding:10px 25px;border-radius:25px;font-weight:600;box-shadow:0 4px 15px rgba(255,87,34,0.3);font-size:15px}"
        ".item{padding:12px 15px;margin:8px 0;border-bottom:1px solid #e0e0e0;display:flex;justify-content:space-between;font-size:17px;transition:background 0.2s}"
        ".item:hover{background:#f5f5f5;border-radius:8px}"
        ".item:last-child{border-bottom:none}"
        ".total{font-size:24px;font-weight:700;color:#FF5722;margin:20px 0;padding:15px;background:#fff3e0;border-radius:10px;text-align:right;border:2px solid #FFB74D}"
        ".bill-total{font-size:28px;font-weight:700;color:#2196F3;padding:25px;background:white;border-radius:12px;margin:20px 0;text-align:right;border:3px solid #64B5F6;box-shadow:0 4px 15px rgba(33,150,243,0.2)}"
        ".btn{padding:14px 30px;border:none;border-radius:10px;font-size:17px;font-weight:600;cursor:pointer;margin:8px;transition:all 0.3s ease;box-shadow:0 4px 12px rgba(0,0,0,0.15)}"
        ".btn:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,0.25)}"
        ".btn:active{transform:translateY(0);box-shadow:0 3px 10px rgba(0,0,0,0.2)}"
        ".btn-accept{background:linear-gradient(135deg,#4CAF50,#66BB6A);color:white}"
        ".btn-accept:hover{background:linear-gradient(135deg,#66BB6A,#4CAF50)}"
        ".btn-reject{background:linear-gradient(135deg,#9E9E9E,#757575);color:white}"
        ".btn-reject:hover{background:linear-gradient(135deg,#757575,#9E9E9E)}"
        ".btn-done{background:linear-gradient(135deg,#9C27B0,#BA68C8);color:white;font-size:18px}"
        ".btn-done:hover{background:linear-gradient(135deg,#BA68C8,#9C27B0)}"
        ".btn-bill{background:linear-gradient(135deg,#FFC107,#FFD54F);color:#000;font-size:24px;padding:20px 60px;font-weight:700}"
        ".btn-bill:hover{background:linear-gradient(135deg,#FFD54F,#FFC107)}"
        ".btn-verify{background:linear-gradient(135deg,#4CAF50,#66BB6A);color:white;font-size:24px;padding:20px 60px;font-weight:700}"
        ".btn-verify:hover{background:linear-gradient(135deg,#66BB6A,#4CAF50)}"
        ".status{padding:12px 25px;border-radius:25px;font-weight:600;display:inline-block;font-size:16px;box-shadow:0 3px 10px rgba(0,0,0,0.2)}"
        ".status-pending{background:linear-gradient(135deg,#FF5722,#FF6B6B);color:white}"
        ".status-preparing{background:linear-gradient(135deg,#2196F3,#42A5F5);color:white}"
        ".status-ready{background:linear-gradient(135deg,#9C27B0,#BA68C8);color:white}"
        ".status-cancelled{background:#9E9E9E;color:white}"
        ".payment-method{background:linear-gradient(135deg,#FFC107,#FFD54F);padding:15px 30px;border-radius:12px;margin:20px 0;font-size:22px;font-weight:700;text-align:center;color:#000;box-shadow:0 4px 15px rgba(255,193,7,0.3)}"
        ".section-title{font-size:20px;color:#666;margin:30px 0 15px 0;padding-bottom:10px;border-bottom:3px solid #ddd}"
        ".empty-state{text-align:center;padding:100px 40px;background:white;border-radius:20px;box-shadow:0 10px 30px rgba(0,0,0,0.1)}"
        ".empty-state h2{color:#bbb;font-size:32px;margin-bottom:15px}"
        ".empty-state p{color:#ddd;font-size:20px}"
        "@media(max-width:768px){h1{font-size:24px;padding:20px}.order-id{font-size:22px}.btn{padding:12px 20px;font-size:15px}}"
        "</style>"
        "<script>"
        "function act(a,id){fetch(a+'?id='+id,{method:'POST'}).then(r=>r.json()).then(()=>location.reload()).catch(e=>{console.error(e);location.reload()})}"
        "setInterval(()=>location.reload(),5000)"
        "</script>"
        "</head><body><div class='container'>"
        "<h1>🍽️ Chef Monitor Dashboard</h1>"
    );
    
    // Display Bill Requests (Highest Priority)
    bool has_bill_requests = false;
    for (int i = bill_count - 1; i >= 0; i--) {
        if (bills[i].bill_requested && !bills[i].generated) {
            if (!has_bill_requests) {
                pos += snprintf(html + pos, max_size - pos, "<div class='section-title'>⚡ URGENT BILL REQUESTS</div>");
                has_bill_requests = true;
            }
            pos += snprintf(html + pos, max_size - pos,
                "<div class='bill-request-card'>"
                "<div class='order-header'>"
                "<h2 style='color:#F57C00;margin:0;font-size:26px'>💳 BILL REQUEST - Section %d | Table %d</h2>"
                "</div>"
                "<p style='font-size:22px;color:#F57C00;text-align:center;margin:25px 0;font-weight:600'>"
                "🔔 Customer is waiting for bill approval!"
                "</p>"
                "<div style='text-align:center;margin-top:30px'>"
                "<button class='btn btn-bill' onclick='act(\"/generate_bill\",%d)'>📄 GENERATE BILL NOW</button>"
                "</div></div>",
                bills[i].section_id, bills[i].table_number, bills[i].section_id
            );
        }
    }
    
    // Display Generated Bills
    bool has_bills = false;
    for (int i = bill_count - 1; i >= 0; i--) {
        if (bills[i].generated && !bills[i].payment_verified) {
            if (!has_bills) {
                pos += snprintf(html + pos, max_size - pos, "<div class='section-title'>💰 PENDING BILLS</div>");
                has_bills = true;
            }
            pos += snprintf(html + pos, max_size - pos,
                "<div class='bill-card'>"
                "<div class='order-header'>"
                "<h2 style='color:#1976D2;margin:0;font-size:26px'>🧾 BILL - Section %d | Table %d</h2>"
                "</div>"
                "<h3 style='margin:20px 0 15px 0;color:#444;font-size:20px'>📋 Order Items:</h3>",
                bills[i].section_id, bills[i].table_number
            );
            
            for (int j = 0; j < bills[i].item_count && pos < max_size - 1500; j++) {
                pos += snprintf(html + pos, max_size - pos,
                    "<div class='item'><span>%s</span><span><strong>₹%d</strong></span></div>",
                    bills[i].items[j], bills[i].prices[j]
                );
            }
            
            pos += snprintf(html + pos, max_size - pos,
                "<div class='bill-total'>"
                "Subtotal: <strong>₹%d</strong><br>"
                "GST (18%%): <strong>₹%d</strong><br>"
                "<hr style='border:2px solid #64B5F6;margin:15px 0'>"
                "💵 GRAND TOTAL: <strong style='font-size:36px;color:#FF5722'>₹%d</strong>"
                "</div>",
                bills[i].subtotal, bills[i].gst, bills[i].total
            );
            
            if (strcmp(bills[i].payment_method, "pending") != 0) {
                pos += snprintf(html + pos, max_size - pos,
                    "<div class='payment-method'>💳 Payment Method: %s</div>",
                    bills[i].payment_method
                );
            } else {
                pos += snprintf(html + pos, max_size - pos,
                    "<div style='text-align:center;color:#888;font-size:19px;margin:20px 0;font-style:italic'>"
                    "⏳ Waiting for customer to select payment method..."
                    "</div>"
                );
            }
            
            pos += snprintf(html + pos, max_size - pos,
                "<div style='text-align:center;margin-top:30px'>"
                "<button class='btn btn-verify' onclick='act(\"/verify\",%d)'>✅ VERIFY PAYMENT</button>"
                "</div></div>",
                bills[i].section_id
            );
        }
    }
    
    // Display Active Orders
    bool has_orders = false;
    for (int i = order_count - 1; i >= 0 && pos < max_size - 2000; i--) {
        if (orders[i].status == ORDER_COMPLETED) continue;
        
        if (!has_orders) {
            pos += snprintf(html + pos, max_size - pos, "<div class='section-title'>📦 ACTIVE ORDERS</div>");
            has_orders = true;
        }
        
        const char *status_text = "🆕 NEW ORDER";
        const char *status_class = "status-pending";
        const char *card_class = "";
        
        if (orders[i].status == ORDER_REJECTED) {
            status_text = "❌ CANCELLED";
            status_class = "status-cancelled";
            card_class = "cancelled";
        } else if (orders[i].status == ORDER_ACCEPTED || orders[i].status == ORDER_PREPARING) {
            status_text = "👨‍🍳 PREPARING";
            status_class = "status-preparing";
            card_class = "accepted";
        } else if (orders[i].status == ORDER_READY) {
            status_text = "✅ READY";
            status_class = "status-ready";
            card_class = "ready";
        }
        
        pos += snprintf(html + pos, max_size - pos,
            "<div class='order-card %s'>"
            "<div class='order-header'>"
            "<div><span class='order-id'>Order #%d</span> <span class='status %s'>%s</span></div>"
            "<span class='table-badge'>🪑 Table %d | Section %d</span>"
            "</div><h3 style='margin:15px 0;color:#555;font-size:18px'>🍽️ Items:</h3>",
            card_class, orders[i].id, status_class, status_text, 
            orders[i].table_number, orders[i].section_id
        );
        
        for (int j = 0; j < orders[i].item_count && pos < max_size - 800; j++) {
            pos += snprintf(html + pos, max_size - pos,
                "<div class='item'><span>%s</span><span><strong>₹%d</strong></span></div>",
                orders[i].items[j], orders[i].prices[j]
            );
        }
        
        pos += snprintf(html + pos, max_size - pos,
            "<div class='total'>💰 Total: ₹%d</div><div style='text-align:center;margin-top:20px'>",
            orders[i].total_price
        );
        
        if (orders[i].status == ORDER_PENDING) {
            pos += snprintf(html + pos, max_size - pos,
                "<button class='btn btn-accept' onclick='act(\"/accept\",%d)'>✅ Accept Order</button>"
                "<button class='btn btn-reject' onclick='act(\"/reject\",%d)'>❌ Cancel Order</button>",
                orders[i].id, orders[i].id
            );
        } else if (orders[i].status == ORDER_ACCEPTED || orders[i].status == ORDER_PREPARING) {
            pos += snprintf(html + pos, max_size - pos,
                "<button class='btn btn-done' onclick='act(\"/done\",%d)'>🍴 Food Ready</button>",
                orders[i].id
            );
        }
        
        pos += snprintf(html + pos, max_size - pos, "</div></div>");
    }
    
    if (!has_orders && !has_bills && !has_bill_requests) {
        pos += snprintf(html + pos, max_size - pos,
            "<div class='empty-state'>"
            "<h2>😴 No Active Orders</h2>"
            "<p>Waiting for new orders...</p>"
            "</div>"
        );
    }
    
    snprintf(html + pos, max_size - pos,
        "<p style='text-align:center;color:rgba(255,255,255,0.8);margin-top:50px;font-size:15px'>"
        "🔄 Auto-refresh every 5 seconds | ✅ Connected</p>"
        "</div></body></html>"
    );
}


/* ==================== HTTP HANDLERS ==================== */
static esp_err_t root_handler(httpd_req_t *req)
{
    char *html = malloc(40000);
    if (html == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    build_webpage(html, 40000);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    free(html);
    
    return ESP_OK;
}

static esp_err_t order_handler(httpd_req_t *req)
{
    char *content = malloc(2048);
    if (content == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int total_len = req->content_len;
    int cur_len = 0;
    
    if (total_len >= 2048) {
        free(content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }
    
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, content + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(content);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    content[cur_len] = '\0';
    
    ESP_LOGI(TAG, "Received order: %s", content);
    
    cJSON *json = cJSON_Parse(content);
    free(content);
    
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *table = cJSON_GetObjectItem(json, "table");
    cJSON *section = cJSON_GetObjectItem(json, "section");
    cJSON *items = cJSON_GetObjectItem(json, "items");
    cJSON *prices = cJSON_GetObjectItem(json, "prices");
    
    int order_id = -1;
    
    if (cJSON_IsNumber(table) && cJSON_IsNumber(section) && 
        cJSON_IsArray(items) && cJSON_IsArray(prices)) {
        order_id = create_order(table->valueint, section->valueint, items, prices);
    }
    
    cJSON_Delete(json);
    
    if (order_id > 0) {
        char response[128];
        snprintf(response, sizeof(response), "{\"status\":\"success\",\"order_id\":%d}", order_id);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(req, response);
        return ESP_OK;
    }
    
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Order creation failed");
    return ESP_FAIL;
}

static esp_err_t accept_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            if (update_order_status(atoi(param), ORDER_ACCEPTED)) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"success\"}");
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid");
    return ESP_FAIL;
}

static esp_err_t reject_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            if (update_order_status(atoi(param), ORDER_REJECTED)) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"success\"}");
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid");
    return ESP_FAIL;
}

static esp_err_t done_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            if (update_order_status(atoi(param), ORDER_READY)) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"success\"}");
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid");
    return ESP_FAIL;
}

// NEW: Request bill endpoint
static esp_err_t request_bill_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "section", param, sizeof(param)) == ESP_OK) {
            if (request_bill(atoi(param))) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_sendstr(req, "{\"status\":\"success\"}");
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bill request failed");
    return ESP_FAIL;
}

static esp_err_t generate_bill_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            int result = generate_bill(atoi(param));
            if (result > 0) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"success\"}");
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bill generation failed");
    return ESP_FAIL;
}

static esp_err_t payment_method_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    ESP_LOGI(TAG, "Payment method received: %s", content);
    
    cJSON *json = cJSON_Parse(content);
    if (json != NULL) {
        cJSON *section = cJSON_GetObjectItem(json, "section");
        cJSON *method = cJSON_GetObjectItem(json, "method");
        
        if (cJSON_IsNumber(section) && cJSON_IsString(method)) {
            if (update_payment_method(section->valueint, method->valuestring)) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_sendstr(req, "{\"status\":\"success\"}");
                cJSON_Delete(json);
                return ESP_OK;
            }
        }
        cJSON_Delete(json);
    }
    
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid");
    return ESP_FAIL;
}

static esp_err_t verify_payment_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            if (verify_payment(atoi(param))) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, "{\"status\":\"success\"}");
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Verification failed");
    return ESP_FAIL;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            order_t *order = get_order_by_id(atoi(param));
            if (order != NULL) {
                char response[128];
                snprintf(response, sizeof(response), 
                    "{\"status\":\"success\",\"order_status\":%d}", order->status);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_sendstr(req, response);
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Order not found");
    return ESP_FAIL;
}

// NEW: Check if bill is generated
static esp_err_t check_bill_status_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "section", param, sizeof(param)) == ESP_OK) {
            int section_id = atoi(param);
            bill_t *bill = get_bill_by_section(section_id);
            
            if (bill != NULL && bill->generated) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_sendstr(req, "{\"bill_generated\":true}");
            } else {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_sendstr(req, "{\"bill_generated\":false}");
            }
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
}

static esp_err_t get_bill_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "section", param, sizeof(param)) == ESP_OK) {
            int section_id = atoi(param);
            ESP_LOGI(TAG, "Bill requested for section: %d", section_id);
            
            bill_t *bill = get_bill_by_section(section_id);
            
            if (bill != NULL && bill->generated) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "subtotal", bill->subtotal);
                cJSON_AddNumberToObject(json, "gst", bill->gst);
                cJSON_AddNumberToObject(json, "total", bill->total);
                cJSON_AddNumberToObject(json, "item_count", bill->item_count);
                
                cJSON *items = cJSON_CreateArray();
                cJSON *prices = cJSON_CreateArray();
                for (int i = 0; i < bill->item_count; i++) {
                    cJSON_AddItemToArray(items, cJSON_CreateString(bill->items[i]));
                    cJSON_AddItemToArray(prices, cJSON_CreateNumber(bill->prices[i]));
                }
                cJSON_AddItemToObject(json, "items", items);
                cJSON_AddItemToObject(json, "prices", prices);
                
                char *response = cJSON_PrintUnformatted(json);
                ESP_LOGI(TAG, "Sending bill: %s", response);
                
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_sendstr(req, response);
                free(response);
                cJSON_Delete(json);
                return ESP_OK;
            }
        }
    }
    
    ESP_LOGW(TAG, "Bill not found or not generated");
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Bill not found");
    return ESP_FAIL;
}

static esp_err_t check_payment_verified_handler(httpd_req_t *req)
{
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(query, "section", param, sizeof(param)) == ESP_OK) {
            int section_id = atoi(param);
            bill_t *bill = get_bill_by_section(section_id);
            
            if (bill != NULL && bill->payment_verified) {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_sendstr(req, "{\"verified\":true}");
            } else {
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_sendstr(req, "{\"verified\":false}");
            }
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
}

/* ==================== SERVER SETUP ==================== */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 20;
    config.stack_size = 12288;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.max_resp_headers = 16;
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Server start failed");
        return NULL;
    }
    
    httpd_uri_t uris[] = {
        {"/", HTTP_GET, root_handler, NULL},
        {"/order", HTTP_POST, order_handler, NULL},
        {"/accept", HTTP_POST, accept_handler, NULL},
        {"/reject", HTTP_POST, reject_handler, NULL},
        {"/done", HTTP_POST, done_handler, NULL},
        {"/request_bill", HTTP_POST, request_bill_handler, NULL},         // NEW
        {"/generate_bill", HTTP_POST, generate_bill_handler, NULL},
        {"/payment_method", HTTP_POST, payment_method_handler, NULL},
        {"/verify", HTTP_POST, verify_payment_handler, NULL},
        {"/status", HTTP_GET, status_handler, NULL},
        {"/check_bill_status", HTTP_GET, check_bill_status_handler, NULL}, // NEW
        {"/get_bill", HTTP_GET, get_bill_handler, NULL},
        {"/check_payment", HTTP_GET, check_payment_verified_handler, NULL}
    };
    
    for (int i = 0; i < 13; i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    
    ESP_LOGI(TAG, "HTTP Server started successfully");
    return server;
}

/* ==================== WIFI ==================== */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Client connected");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Client disconnected");
    }
}

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WiFi AP Started");
    ESP_LOGI(TAG, "SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "Password: %s", WIFI_PASS);
    ESP_LOGI(TAG, "IP Address: 192.168.4.1");
    ESP_LOGI(TAG, "========================================");
}

/* ==================== MAIN ==================== */
void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   RESTAURANT HOST SERVER STARTING");
    ESP_LOGI(TAG, "========================================");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    wifi_init_softap();
    vTaskDelay(pdMS_TO_TICKS(2000));
    start_webserver();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   SYSTEM READY - ACCEPTING ORDERS");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
}
