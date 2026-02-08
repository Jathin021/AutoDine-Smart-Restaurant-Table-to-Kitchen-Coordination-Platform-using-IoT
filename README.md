# AutoDine v3.0

**Smart Restaurant Table-to-Kitchen Coordination Platform using IoT**

---

## 📋 Project Overview

AutoDine is an ESP32-based IoT platform that enables autonomous table-side food ordering and real-time coordination between restaurant tables and the kitchen. The system reduces manual order-taking by allowing customers to place orders directly from table-mounted embedded units, while kitchen staff manages orders through a browser-based dashboard.

**Version:** 3.0  
**Owner:** Jathin Pusuluri

---

## 🏗️ System Architecture

### Hardware Components

**Table Units (ESP32 × 2+)**
- ESP32 Dev Module
- 0.96" SSD1306 OLED Display (128×64, I2C)
- 4× Push Buttons (Menu navigation, quantity, confirm, back)
- WiFi connectivity

**Host Unit (ESP32 × 1)**
- ESP32 Dev Module
- Buzzer (order notifications)
- WiFi connectivity
- Embedded web server

### Software Stack
- **Framework:** ESP-IDF v5.5.2
- **Language:** C
- **Communication:** WiFi, HTTP, JSON
- **Display:** SSD1306 OLED driver
- **Web Interface:** HTML, CSS, JavaScript

---

## ✨ Features

### Customer Experience (Table Unit)
- ✅ OLED menu browsing with 0.96" display optimization
- ✅ Button-driven item selection and quantity adjustment
- ✅ Real-time order status updates
- ✅ Professional bill display with GST calculation
- ✅ UPI QR code payment display
- ✅ Cash/Card payment option
- ✅ Add more items after food preparation
- ✅ Yellow region avoidance for QR display

### Kitchen Management (Host Dashboard)
- ✅ Real-time order notifications with buzzer
- ✅ Multi-table support (scalable)
- ✅ Order accept/decline functionality
- ✅ Food preparation status updates
- ✅ Professional bill generation with itemized details
- ✅ Payment method tracking
- ✅ Order completion workflow
- ✅ Responsive web interface

### Technical Features
- ✅ Dual cart system (pending + accepted orders)
- ✅ State machine-based workflow
- ✅ HTTP polling for status updates
- ✅ JSON-based data exchange
- ✅ Modular and scalable architecture
- ✅ OLED readability optimizations
- ✅ Professional UI/UX design

---

## 🔄 Complete Workflow (v3.0)

### A. Initial Order Placement

1. **Idle State**
   - Customer presses any button → Menu opens at Item #1

2. **Menu Browsing**
   - Button-1 (UP): Previous item
   - Button-2 (DOWN): Next item
   - Button-3 (OK): Select item
   - Button-4 (BACK): Send order

3. **Quantity Selection**
   - Button-2: Increase quantity (1-15)
   - Button-1: Decrease quantity
   - Button-3: Add to cart
   - Button-4: Back to menu

4. **Order Submission**
   - Press Button-4 from menu → Order sent to kitchen
   - OLED shows "Waiting for order to be accepted"

### B. Kitchen Processing

5. **Dashboard Notification**
   - Buzzer sounds
   - Order appears with ACCEPT/DECLINE buttons

6. **Chef Decision**
   - **ACCEPT** → Table notified, cooking begins
   - **DECLINE** → Table notified, order cancelled

### C. Food Preparation

7. **Cooking State**
   - OLED shows "Food is preparing in 10-15 min"
   - Button-3 available to add more items

8. **Food Ready**
   - Chef clicks "Food Prepared" button
   - OLED shows "Food is prepared! Enjoy meal"
   - Button-3: Add more items
   - Button-4: Request bill

### D. Additional Orders (Optional)

9. **Add More Items**
   - Customer can add items after food is prepared
   - New items go through same accept/decline flow
   - Merged with existing order

### E. Billing & Payment

10. **Bill Request**
    - Customer presses Button-4 → Bill requested
    - Dashboard shows "Generate Bill" button

11. **Bill Display**
    - OLED shows: Subtotal, GST (18%), Total
    - Dashboard shows itemized bill with all details
    - Any button → Payment method selection

12. **Payment Method**
    - Button-1: UPI (shows QR code)
    - Button-2: Cash/Card (pay at counter)

13. **Payment Completion**
    - Dashboard marks payment complete
    - OLED shows "Thank you! Visit again"
    - Table resets to idle

---

## 🛠️ Build Instructions

### Prerequisites
- ESP-IDF v5.5.2
- Git
- USB drivers for ESP32

### Table Unit Setup

1. **Clone Repository**
   ```bash
   cd d:\e\Major_Project\AutoDine_Table
   ```

2. **Configure WiFi & Host IP**
   Edit `main/app_config.h`:
   ```c
   #define WIFI_SSID "YourWiFiName"
   #define WIFI_PASSWORD "YourPassword"
   #define HOST_IP "192.168.x.x"  // Host ESP32 IP
   #define TABLE_ID 1  // Change to 2, 3, etc. for additional tables
   ```

3. **Build & Flash**
   ```bash
   idf.py build
   idf.py flash
   idf.py monitor
   ```

### Host Unit Setup

1. **Navigate to Host Directory**
   ```bash
   cd d:\e\Major_Project\AutoDine_Host
   ```

2. **Configure WiFi**
   Edit `main/app_config.h`:
   ```c
   #define WIFI_SSID "YourWiFiName"
   #define WIFI_PASSWORD "YourPassword"
   ```

3. **Build & Flash**
   ```bash
   idf.py build
   idf.py flash
   idf.py monitor
   ```

4. **Access Dashboard**
   - Note the IP address from monitor output
   - Open browser: `http://<HOST_IP>`

---

## 🔧 Hardware Connections

### Table Unit
```
ESP32          Component
GPIO 21   →    OLED SDA
GPIO 22   →    OLED SCL
GPIO 32   →    Button-1 (UP/Decrease)
GPIO 33   →    Button-2 (DOWN/Increase)
GPIO 25   →    Button-3 (OK/Confirm)
GPIO 26   →    Button-4 (BACK/Send)
3.3V      →    OLED VCC
GND       →    OLED GND, All Buttons (other pin)
```

### Host Unit
```
ESP32          Component
GPIO 23   →    Buzzer (+)
GND       →    Buzzer (-)
```

---

## 📱 Dashboard Features

- **Table Status Cards:** Real-time status for each table
- **Order Management:** Accept, decline, mark prepared
- **Bill Generation:** Professional itemized bills
- **Payment Tracking:** UPI/Cash/Card status
- **Responsive Design:** Works on desktop and mobile
- **Footer:** "AutoDine Host System v3.0 | Owner Credits: Jathin Pusuluri"

---

## 🎨 OLED UI Optimizations

### Display Specifications
- **Size:** 0.96 inch
- **Resolution:** 128×64 pixels
- **Type:** Monochrome SSD1306
- **Yellow Region:** Top 16 pixels (hardware fixed)

### UI Design Principles
- Maximum 3 lines of content per screen
- 16 characters per line maximum
- Generous vertical spacing (12px)
- Clear visual hierarchy (main text vs hints)
- Minimal horizontal dividers
- QR code avoids yellow region (renders from Y=16)

---

## 🔑 Key Technical Decisions

1. **Dual Cart System:** Separate pending and accepted carts prevent duplication
2. **State Machine:** Clean workflow management across all states
3. **Polling Architecture:** Table units poll host for status updates
4. **Button Remapping:** B2=Increase, B1=Decrease for intuitive quantity selection
5. **Simplified Bill:** OLED shows totals only; dashboard shows full itemization
6. **Yellow Region Handling:** QR code specifically positioned to avoid yellow area

---

## 📂 Project Structure

```
Major_Project/
├── AutoDine_Table/          # Table unit firmware
│   ├── main/
│   │   ├── main.c           # Main logic & state machine
│   │   ├── hardware.c       # OLED & button drivers
│   │   ├── ui_display.c     # UI rendering
│   │   ├── cart.c           # Cart management
│   │   ├── menu_data.c      # Menu database
│   │   └── app_config.h     # Configuration
│   └── CMakeLists.txt
│
├── AutoDine_Host/           # Host unit firmware
│   ├── main/
│   │   ├── main.c           # HTTP server & logic
│   │   └── web/             # Dashboard files
│   │       ├── index.html
│   │       ├── style.css
│   │       └── app.js
│   └── CMakeLists.txt
│
└── README.md                # This file
```

---

## 🚀 Scaling to Multiple Tables

To add more tables:

1. Flash Table firmware to new ESP32
2. Change `TABLE_ID` in `app_config.h` (2, 3, 4, etc.)
3. Connect to same WiFi network
4. Dashboard automatically recognizes new tables

---

## 🎯 Applications

- Smart restaurants and cafes
- Self-service dining systems
- Food courts and canteens
- IoT demonstrations
- Academic projects
- Embedded systems learning

---

## 📊 Version History

- **v3.0** - Complete workflow with billing, payments, OLED optimizations
- **v2.0** - Multi-table support, dashboard improvements
- **v1.0** - Initial prototype with basic ordering

---

## 🔮 Future Enhancements

- Cloud analytics and reporting
- Mobile app integration
- Touchscreen interface
- Multi-language support
- Kitchen display system (KDS)
- Inventory management integration

---

## 📄 License

This project is developed for educational and demonstration purposes.

---

## 👨‍💻 Author

**Jathin Pusuluri**

AutoDine is developed as an embedded IoT learning project focused on real-world restaurant workflow automation using ESP32 and ESP-IDF.

---

## 🙏 Acknowledgments

- ESP-IDF Framework
- SSD1306 OLED Library
- cJSON Library

---

**AutoDine v3.0** - Deployment Ready ✅
