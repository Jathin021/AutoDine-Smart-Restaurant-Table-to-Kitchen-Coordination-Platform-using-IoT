#include "http_server.h"
#include "order_manager.h"
#include "buzzer.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "HTTP_SERVER";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");

// Serve index.html
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

// Serve style.css
static esp_err_t style_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)style_css_start, style_css_end - style_css_start);
    return ESP_OK;
}

// Serve app.js
static esp_err_t app_js_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);
    return ESP_OK;
}

// POST /api/order
static esp_err_t api_order_handler(httpd_req_t *req) {
    char buf[2048];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    cJSON *append_obj = cJSON_GetObjectItem(root, "append");
     bool is_append = (append_obj && cJSON_IsTrue(append_obj));

// Log it (order_manager already handles appending automatically now)
     ESP_LOGI(TAG, "Order received: append=%s", is_append ? "true" : "false");
    cJSON *table_id_obj = cJSON_GetObjectItem(root, "table_id");
    cJSON *items = cJSON_GetObjectItem(root, "items");
    cJSON *total_obj = cJSON_GetObjectItem(root, "total");

    if (!table_id_obj || !items || !total_obj) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
        return ESP_FAIL;
    }

    uint8_t table_id = table_id_obj->valueint;
    uint32_t total = total_obj->valueint;

    uint8_t order_id = order_manager_create_order(table_id, items, total);
    cJSON_Delete(root);

    if (order_id == 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Order creation failed");
        return ESP_FAIL;
    }

    // Order remains PENDING - chef must explicitly accept/decline
    buzzer_beep_order();

    const char *response = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
    
}

// GET /api/table_status?table_id=1
static esp_err_t api_table_status_handler(httpd_req_t *req) {
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
        return ESP_FAIL;
    }

    char table_id_str[8];
    if (httpd_query_key_value(query, "table_id", table_id_str, sizeof(table_id_str)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing table_id");
        return ESP_FAIL;
    }

    uint8_t table_id = atoi(table_id_str);
    table_info_t *table = order_manager_get_table_info(table_id);

    if (!table) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Table not found");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "table_id", table->table_id);
    cJSON_AddStringToObject(root, "status",
        table->status == TABLE_STATUS_IDLE ? "idle" :
        table->status == TABLE_STATUS_COOKING ? "cooking" :
        table->status == TABLE_STATUS_PREPARED ? "prepared" :
        table->status == TABLE_STATUS_BILLING ? "billing" : "payment");
    cJSON_AddStringToObject(root, "order_state",
        table->order_state == ORDER_STATE_NONE ? "none" :
        table->order_state == ORDER_STATE_PENDING ? "pending" :
        table->order_state == ORDER_STATE_ACCEPTED ? "accepted" :
        table->order_state == ORDER_STATE_DECLINED ? "declined" : "prepared");

    if (strlen(table->bill_json) > 0) {
        cJSON_AddStringToObject(root, "bill_data", table->bill_json);
    } else {
        cJSON_AddNullToObject(root, "bill_data");
    }

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

// POST /api/request_bill
static esp_err_t api_request_bill_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *table_id_obj = cJSON_GetObjectItem(root, "table_id");
    if (!table_id_obj) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing table_id");
        return ESP_FAIL;
    }

    uint8_t table_id = table_id_obj->valueint;
    cJSON_Delete(root);

    buzzer_beep_bill();

    if (!order_manager_generate_bill(table_id)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Bill generation failed");
        return ESP_FAIL;
    }

    const char *response = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

// POST /api/payment
static esp_err_t api_payment_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *table_id_obj = cJSON_GetObjectItem(root, "table_id");
    cJSON *method_obj = cJSON_GetObjectItem(root, "method");

    if (!table_id_obj || !method_obj) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
        return ESP_FAIL;
    }

    uint8_t table_id = table_id_obj->valueint;
    const char *method = method_obj->valuestring;

    order_manager_set_payment_method(table_id, method);
    cJSON_Delete(root);

    const char *response = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

// POST /api/chef/accept
static esp_err_t api_chef_accept_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *order_id_obj = cJSON_GetObjectItem(root, "order_id");
    if (!order_id_obj) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing order_id");
        return ESP_FAIL;
    }

    uint8_t order_id = order_id_obj->valueint;
    cJSON_Delete(root);

    if (!order_manager_accept_order(order_id)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Accept failed");
        return ESP_FAIL;
    }

    const char *response = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

// POST /api/chef/decline
static esp_err_t api_chef_decline_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *order_id_obj = cJSON_GetObjectItem(root, "order_id");
    if (!order_id_obj) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing order_id");
        return ESP_FAIL;
    }

    uint8_t order_id = order_id_obj->valueint;
    cJSON_Delete(root);

    if (!order_manager_decline_order(order_id)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Decline failed");
        return ESP_FAIL;
    }

    const char *response = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

// POST /api/chef/food_prepared
static esp_err_t api_chef_food_prepared_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *order_id_obj = cJSON_GetObjectItem(root, "order_id");
    if (!order_id_obj) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing order_id");
        return ESP_FAIL;
    }

    uint8_t order_id = order_id_obj->valueint;
    cJSON_Delete(root);

    if (!order_manager_mark_prepared(order_id)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Mark prepared failed");
        return ESP_FAIL;
    }

    const char *response = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

// POST /api/chef/verify_payment
static esp_err_t api_chef_verify_payment_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *table_id_obj = cJSON_GetObjectItem(root, "table_id");
    if (!table_id_obj) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing table_id");
        return ESP_FAIL;
    }

    uint8_t table_id = table_id_obj->valueint;
    cJSON_Delete(root);

    if (!order_manager_verify_payment(table_id)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Verify payment failed");
        return ESP_FAIL;
    }

    const char *response = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

// GET /api/dashboard/tables
static esp_err_t api_dashboard_tables_handler(httpd_req_t *req) {
    char buffer[4096];
    order_manager_get_all_tables_json(buffer, sizeof(buffer));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, strlen(buffer));

    return ESP_OK;
}

httpd_handle_t http_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_index = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_index);

        httpd_uri_t uri_style = {
            .uri = "/style.css",
            .method = HTTP_GET,
            .handler = style_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_style);

        httpd_uri_t uri_app_js = {
            .uri = "/app.js",
            .method = HTTP_GET,
            .handler = app_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_app_js);

        httpd_uri_t uri_api_order = {
            .uri = "/api/order",
            .method = HTTP_POST,
            .handler = api_order_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_order);

        httpd_uri_t uri_api_table_status = {
            .uri = "/api/table_status",
            .method = HTTP_GET,
            .handler = api_table_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_table_status);

        httpd_uri_t uri_api_request_bill = {
            .uri = "/api/request_bill",
            .method = HTTP_POST,
            .handler = api_request_bill_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_request_bill);

        httpd_uri_t uri_api_payment = {
            .uri = "/api/payment",
            .method = HTTP_POST,
            .handler = api_payment_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_payment);

        httpd_uri_t uri_api_chef_accept = {
            .uri = "/api/chef/accept",
            .method = HTTP_POST,
            .handler = api_chef_accept_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_chef_accept);

        httpd_uri_t uri_api_chef_decline = {
            .uri = "/api/chef/decline",
            .method = HTTP_POST,
            .handler = api_chef_decline_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_chef_decline);

        httpd_uri_t uri_api_chef_food_prepared = {
            .uri = "/api/chef/food_prepared",
            .method = HTTP_POST,
            .handler = api_chef_food_prepared_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_chef_food_prepared);

        httpd_uri_t uri_api_chef_verify_payment = {
            .uri = "/api/chef/verify_payment",
            .method = HTTP_POST,
            .handler = api_chef_verify_payment_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_chef_verify_payment);

        httpd_uri_t uri_api_dashboard_tables = {
            .uri = "/api/dashboard/tables",
            .method = HTTP_GET,
            .handler = api_dashboard_tables_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_api_dashboard_tables);

        ESP_LOGI(TAG, "HTTP server started successfully");
        return server;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

void http_server_stop(httpd_handle_t server) {
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}
