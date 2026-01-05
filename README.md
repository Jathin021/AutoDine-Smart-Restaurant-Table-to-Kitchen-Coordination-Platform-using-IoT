🚀 AUTODINE:
🍽️ Smart Restaurant Table–Kitchen Coordination Platform (IoT)

✦ Overview

    AutoDine is an ESP32-based IoT platform that enables autonomous table-side food ordering and real-time coordination between restaurant tables and the kitchen.

    Customers place orders using a table-mounted embedded unit, while kitchen staff receive and manage orders through a browser-based dashboard hosted on an ESP32.

    The project demonstrates how embedded systems and IoT can modernize restaurant workflows in a cost-effective and scalable manner.

🧩 System Architecture

    AutoDine follows a distributed embedded architecture using two ESP32 nodes connected via Wi-Fi.

● Table Unit (ESP32)

    OLED display for menu and order status

    Push-button based user interaction

    Sends order data wirelessly

● Host Unit (ESP32)

    Embedded HTTP web server

    Kitchen coordination dashboard

    Sends live order status updates

▶ Workflow

    Menu is displayed on the table OLED

    Customer selects food items using push buttons

    Order data is sent wirelessly to the host ESP32

    Order appears on the kitchen web dashboard

    Kitchen staff updates order status

    Status update is sent back to the table unit

    Customer views live order progress

✔ Key Features

    Autonomous table-side food ordering

    Dual ESP32 distributed architecture

    OLED-based menu and order tracking

    Button-driven customer interaction

    Wi-Fi enabled IoT communication

    Embedded kitchen web dashboard

    Real-time table ↔ kitchen synchronization

⚙ Technology Stack
 🔧 Hardware

     ESP32 DevKit V1

     SSD1306 OLED Display (128×64)

    Push Buttons

 💻 Firmware

    ESP-IDF

    Embedded C

 🌐 Communication

    Wi-Fi

    HTTP

    JSON

 🖥 Interface

    Embedded Web Server

    HTML, CSS

📂 Repository Structure

AutoDine/

├── Restaurant_Menu/     # Table-side ESP32 firmware

├── Restaurant_Host/     # Host ESP32 web server firmware

├── Demo_V1.0.mp4        # Prototype demonstration

└── README.md

🎯 Applications

    Smart restaurants and cafés

    Self-ordering dining environments

    Embedded & IoT demonstrations

    Academic and final-year engineering projects

🚀 Future Enhancements

    Touchscreen-based menu interface

    Multi-table support with unique Table IDs

    Cloud integration for analytics and monitoring

    Mobile application support

    Optional payment and billing integration

📜 License

    This project is open source and intended for educational and research purposes.

👨‍💻 Author

    AutoDine is developed as an embedded IoT learning project, demonstrating real-world table-to-kitchen coordination using ESP32 and ESP-IDF.
