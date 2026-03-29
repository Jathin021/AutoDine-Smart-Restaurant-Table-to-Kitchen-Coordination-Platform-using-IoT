# AutoDine V4.0 - User Manual & Project Report
### *Smart Restaurant Table-to-Kitchen Coordination Platform*

![Cover Image](C:\Users\user\.gemini\antigravity\brain\5a0ef1ef-399c-42e8-9f3a-5e6cf510b67e\autodine_manual_cover_1774784835374.png)

---

## 1. Abstract
AutoDine V4.0 is an advanced IoT solution that digitizes the traditional dining experience. By integrating high-performance HMI displays, real-time cloud databases (Firebase), and autonomous robotics, AutoDine ensures that orders are handled with 100% accuracy and peak efficiency.

---

## 2. System Hardware Components
The following core components power the AutoDine ecosystem:

| Image Reference | Component Name | Purpose |
| :--- | :--- | :--- |
| ![Circuit](C:\Users\user\.gemini\antigravity\brain\5a0ef1ef-399c-42e8-9f3a-5e6cf510b67e\autodine_circuit_diagram_hmi_1774784982554.png) | **CrowPanel 7.0" ESP32-S3 HMI** | The primary Table Unit interface for customers. |
| | **Arduino Uno Rev3** | The brain of the autonomous Waiter Robot. |
| | **GT911 Capacitive Touch** | Provides a smooth, smartphone-like touch experience. |
| | **L298N Motor Driver** | Drives the high-torque motors of the delivery robot. |

---

## 3. Hardware Interfacing (Circuit Guide)

### **Table Unit (ESP32-S3)**
The HMI unit utilizes a 16-bit parallel RGB interface for high-frame-rate animations.
-   **LCD Control**: Pins 0, 39, 40, 41.
-   **Data Bus**: Pins 1-16, 21, 45-48.
-   **Touch I2C**: SDA (19), SCL (20).

### **Waiter Robot (Arduino Uno)**
-   **Motor Left**: Pins 5, 6.
-   **Motor Right**: Pins 10, 11.
-   **Bluetooth Module**: RX/TX via Hardware Serial.

---

## 4. Operational Workflow

### **Step 1: Customer Order Selection**
Customers browse the menu on the table unit. The UI uses **Glassmorphism design** for a premium feel.
1.  Add items to cart.
2.  Select quantity.
3.  Tap **Submit**.

### **Step 2: Kitchen Management (Chef Hub)**
The Chef receives the order instantly via Cloud Sync. 

![Chef Dashboard](C:\Users\user\.gemini\antigravity\brain\5a0ef1ef-399c-42e8-9f3a-5e6cf510b67e\autodine_chef_dashboard_ui_1774784859532.png)

1.  **Preparation**: Chef marks the order as "In Progress".
2.  **Dispatch**: Once cooked, clicking "Ready" notifies the waiter robot and the customer.

---

## 5. Software & Cloud Configuration

### **Firebase Integration**
The system uses **Firestore** for real-time synchronization.
-   **Database**: Native Mode.
-   **Authentication**: Service Account JSON.
-   **Monitoring**: Usage must be monitored via the Firebase Console to stay within the 50k monthly free read limit.

### **Server Environment**
The backend is powered by **Python Flask**.
-   **Port**: 5050.
-   **Main Script**: `server.py`.
-   **Dynamic QR**: Served at `/api/qr/static`.

---

## 6. Conclusion
AutoDine V4.0 represents the next generation of hospitality technology. By combining hardware reliability with cloud-native scalability, it provides a robust platform for modernizing any restaurant business.

---
**Prepared By:** Jathin Pusuluri
**Project ID:** AutoDine-IoT-V4.0
