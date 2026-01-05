A U T O D I N E
══════════════════════════════════════════════
Smart Restaurant Table to Kitchen Coordination
Platform using IoT
══════════════════════════════════════════════

PROJECT OVERVIEW
══════════════════════════════════════════════
AutoDine is an ESP32 based IoT platform designed to enable
autonomous table side food ordering and efficient coordination
between restaurant tables and the kitchen.

The system eliminates manual order taking by allowing customers
to place orders directly from a table mounted embedded unit.
Kitchen staff receive and manage orders through a browser based
interface hosted on an ESP32.

The project demonstrates how embedded systems and IoT can be
used to modernize traditional restaurant service workflows in a
cost effective and scalable manner.


SYSTEM ARCHITECTURE
══════════════════════════════════════════════
AutoDine uses a distributed embedded architecture with two
ESP32 nodes connected over WiFi.

TABLE UNIT ESP32
• OLED display for menu and order status
• Push buttons for item selection
• Sends order data wirelessly

HOST UNIT ESP32
• Embedded web server
• Kitchen coordination dashboard
• Sends order status updates to table unit


FEATURES
══════════════════════════════════════════════
• Autonomous table side food ordering
• Dual ESP32 distributed architecture
• OLED based menu and order status display
• Button driven customer interaction
• WiFi enabled IoT communication
• Embedded web dashboard for kitchen staff
• Real time table to kitchen coordination
• Modular and scalable design


WORKING PRINCIPLE
══════════════════════════════════════════════
1. Menu is displayed on the OLED screen at the table
2. Customer selects food items using push buttons
3. Order details are sent via WiFi to the host ESP32
4. Host ESP32 displays the order on kitchen web dashboard
5. Kitchen staff update order status through browser
6. Updated status is sent back to the table ESP32
7. Customer sees live order status on OLED display


TECHNOLOGIES USED
══════════════════════════════════════════════
Hardware
• ESP32 DevKit V1
• SSD1306 OLED Display
• Push Buttons

Firmware
• ESP IDF
• Embedded C

Communication
• WiFi
• HTTP
• JSON

Interface
• Embedded Web Server using HTML and CSS


REPOSITORY STRUCTURE
══════════════════════════════════════════════
Restaurant_Menu
Contains firmware for table side ESP32

Restaurant_Host
Contains firmware for host ESP32 web server

Demo_V1.0.mp4
Working prototype demonstration video

README
Project documentation file


APPLICATIONS
══════════════════════════════════════════════
• Smart restaurants and cafes
• Self ordering dining environments
• Embedded IoT demonstrations
• Academic and final year engineering projects


FUTURE ENHANCEMENTS
══════════════════════════════════════════════
• Touchscreen based menu interface
• Multi table support with unique table IDs
• Cloud integration for monitoring and analytics
• Mobile application support
• Payment and billing integration optional


LICENSE
══════════════════════════════════════════════
This project is open source and intended for
educational and research purposes.


AUTHOR NOTE
══════════════════════════════════════════════
AutoDine is developed as an embedded IoT learning
project demonstrating real world table to kitchen
coordination using ESP32 and ESP IDF.

══════════════════════════════════════════════
END OF README
══════════════════════════════════════════════
