# AutoDine v3.5

**Smart Restaurant Table-to-Kitchen Coordination Platform using IoT**

---

## 📋 Project Overview

AutoDine is an ESP32-based IoT platform that enables autonomous table-side food ordering and real-time coordination between restaurant tables and the kitchen. The system reduces manual order-taking by allowing customers to place orders directly from table-mounted embedded units, while kitchen staff manages orders through a browser-based dashboard. In v3.5, a **4-wheel chassis Waiter Robot** has been added to autonomously deliver food from the kitchen to the customer's table, completing the end-to-end automation loop.

**Version:** 3.5  
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

**Waiter Robot (Arduino × 1)**
- Arduino Uno / Mega
- Adafruit Motor Shield (L293D)
- 4× DC Motors on 4-wheel chassis
- HC-SR04 Ultrasonic Sensor (obstacle avoidance)
- HC-05 Bluetooth Module (command receiver)
- Battery pack / power supply

### Software Stack
- **Framework:** ESP-IDF v5.5.2 (Table & Host), Arduino IDE (Waiter Robot)
- **Language:** C (ESP32), C++ / Arduino (Waiter Robot)
- **Communication:** WiFi, HTTP, JSON, Bluetooth Serial
- **Display:** SSD1306 OLED driver
- **Web Interface:** HTML, CSS, JavaScript
- **Motor Control:** Adafruit Motor Shield library (AFMotor)

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

### Waiter Robot (Food Delivery)
- ✅ 4-wheel chassis autonomous food delivery
- ✅ Bluetooth command reception (HC-05)
- ✅ Timed navigation to Table-1 (100cm) and Table-2 (150cm)
- ✅ Ultrasonic obstacle detection & avoidance (HC-SR04)
- ✅ Automatic pause/resume on obstacle
- ✅ 10-second wait at table for food pickup
- ✅ Calibrated forward/return durations
- ✅ Auto-return to kitchen after delivery
- ✅ State machine-driven navigation

### Technical Features
- ✅ Dual cart system (pending + accepted orders)
- ✅ State machine-based workflow (Table, Host, and Waiter)
- ✅ HTTP polling for status updates
- ✅ JSON-based data exchange
- ✅ Bluetooth serial communication (Waiter)
- ✅ Modular and scalable architecture
- ✅ OLED readability optimizations
- ✅ Professional UI/UX design

---

## 🔄 Complete Workflow (v3.5)

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

### C. Food Preparation & Delivery

7. **Cooking State**
   - OLED shows "Food is preparing in 10-15 min"
   - Button-3 available to add more items

8. **Waiter Robot Dispatch**
   - Chef triggers delivery → Bluetooth command sent ('1' or '2' for target table)
   - Robot moves forward on 4-wheel chassis toward the table
   - Ultrasonic sensor monitors path; pauses if obstacle < 20cm, resumes when clear
   - Robot arrives at table, waits 10 seconds for food pickup
   - Robot auto-returns to kitchen with calibrated return timing

9. **Food Ready**
   - Chef clicks "Food Prepared" button
   - OLED shows "Food is prepared! Enjoy meal"
   - Button-3: Add more items
   - Button-4: Request bill

### D. Additional Orders (Optional)

10. **Add More Items**
    - Customer can add items after food is prepared
    - New items go through same accept/decline flow
    - Merged with existing order

### E. Billing & Payment

11. **Bill Request**
    - Customer presses Button-4 → Bill requested
    - Dashboard shows "Generate Bill" button

12. **Bill Display**
    - OLED shows: Subtotal, GST (18%), Total
    - Dashboard shows itemized bill with all details
    - Any button → Payment method selection

13. **Payment Method**
    - Button-1: UPI (shows QR code)
    - Button-2: Cash/Card (pay at counter)

14. **Payment Completion**
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

### Waiter Robot Setup

1. **Install Adafruit Motor Shield Library**
   - Extract `AutoDine_Waiter/Adafruit-Motor-Shield-library-master.zip` into Arduino `libraries/` folder

2. **Open Sketch**
   - Open `AutoDine_Waiter/main.ino` in Arduino IDE

3. **Configure Table Distances** (in `main.ino`):
   ```cpp
   #define DURATION_TABLE1_FORWARD 5200    // Forward to Table-1 (100cm)
   #define DURATION_TABLE1_RETURN  5600    // Return from Table-1
   #define DURATION_TABLE2_FORWARD 7800    // Forward to Table-2 (150cm)
   #define DURATION_TABLE2_RETURN  8200    // Return from Table-2
   #define MOTOR_SPEED 180                 // Motor speed (0-255)
   #define SAFE_DISTANCE 20                // Obstacle threshold in cm
   ```

4. **Upload**
   - Select board (Arduino Uno/Mega) and port
   - Upload sketch

5. **Bluetooth Pairing**
   - Pair HC-05 module with the host device
   - Send '1' for Table-1 delivery, '2' for Table-2 delivery

4. **Access Dashboard** (Host)
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

### Waiter Robot
```
Arduino        Component
Motor Shield   →    4× DC Motors (M1, M2, M3, M4)
A4 (TRIG)      →    HC-SR04 Trigger
A5 (ECHO)      →    HC-SR04 Echo
A2 (BT_RX)     →    HC-05 TX
A3 (BT_TX)     →    HC-05 RX
5V             →    HC-SR04 VCC, HC-05 VCC
GND            →    HC-SR04 GND, HC-05 GND
Vin/Ext        →    Motor Power Supply (6-12V)
```

---

## 📱 Dashboard Features

- **Table Status Cards:** Real-time status for each table
- **Order Management:** Accept, decline, mark prepared
- **Bill Generation:** Professional itemized bills
- **Payment Tracking:** UPI/Cash/Card status
- **Responsive Design:** Works on desktop and mobile
- **Footer:** "AutoDine Host System v3.5 | Owner Credits: Jathin Pusuluri"

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
2. **State Machine:** Clean workflow management across all states (Table, Host, Waiter)
3. **Polling Architecture:** Table units poll host for status updates
4. **Button Remapping:** B2=Increase, B1=Decrease for intuitive quantity selection
5. **Simplified Bill:** OLED shows totals only; dashboard shows full itemization
6. **Yellow Region Handling:** QR code specifically positioned to avoid yellow area
7. **Timed Navigation:** Waiter robot uses calibrated time-based movement instead of encoders for simplicity
8. **Separate Return Duration:** Forward and return travel times are independently tuned to account for motor/surface differences
9. **Obstacle Pause/Resume:** Elapsed time is tracked so obstacles don't affect total travel distance accuracy

---

## 📂 Project Structure

```
Major_Project/
├── AutoDine_Table/          # Table unit firmware (ESP-IDF)
│   ├── main/
│   │   ├── main.c           # Main logic & state machine
│   │   ├── hardware.c       # OLED & button drivers
│   │   ├── ui_display.c     # UI rendering
│   │   ├── cart.c           # Cart management
│   │   ├── menu_data.c      # Menu database
│   │   └── app_config.h     # Configuration
│   └── CMakeLists.txt
│
├── AutoDine_Host/           # Host unit firmware (ESP-IDF)
│   ├── main/
│   │   ├── main.c           # HTTP server & logic
│   │   └── web/             # Dashboard files
│   │       ├── index.html
│   │       ├── style.css
│   │       └── app.js
│   └── CMakeLists.txt
│
├── AutoDine_Waiter/         # Waiter Robot firmware (Arduino)
│   ├── main.ino             # Robot state machine & motor control
│   └── Adafruit-Motor-Shield-library-master.zip  # Motor library
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

- **v3.5** - Waiter Robot: 4-wheel chassis autonomous food delivery with Bluetooth, obstacle avoidance
- **v3.0** - Complete workflow with billing, payments, OLED optimizations
- **v2.0** - Multi-table support, dashboard improvements
- **v1.0** - Initial prototype with basic ordering

---

## 🔮 Future Enhancements

- Line-following / RFID-based precision navigation for waiter robot
- ESP32-to-Arduino auto-dispatch (Host triggers robot delivery automatically)
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
- Adafruit Motor Shield Library
- Arduino IDE

---

**AutoDine v3.5** - Deployment Ready ✅
