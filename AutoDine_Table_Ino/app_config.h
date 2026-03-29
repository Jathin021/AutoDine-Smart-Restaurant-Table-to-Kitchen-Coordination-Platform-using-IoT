#pragma once
/* =====================================================================
 *  app_config.h — AutoDine V4.0 (Arduino)
 * ===================================================================== */

/* ---------- WiFi ---------- */
#define WIFI_SSID          "iPhone"
#define WIFI_PASS          "12345678"

/* ---------- Server ---------- */
#define SERVER_BASE_URL    "http://172.20.10.4:5050"
#define TABLE_NUMBER       1

/* ---------- LCD ---------- */
#define LCD_H_RES   800
#define LCD_V_RES   480

/* ---------- Timeouts ---------- */
#define NET_TIMEOUT_MS          8000
#define ORDER_POLL_INTERVAL_MS  3000
#define PAYMENT_POLL_MS         3000

/* ---------- Table Label (D1 — eliminate hardcoded strings) ---------- */
#define _STRINGIFY(x)  #x
#define STRINGIFY(x)   _STRINGIFY(x)
#define TABLE_LABEL    "Table " STRINGIFY(TABLE_NUMBER)
/* Upper-case variant used in UI headers */
#define TABLE_LABEL_UC "TABLE " STRINGIFY(TABLE_NUMBER)
