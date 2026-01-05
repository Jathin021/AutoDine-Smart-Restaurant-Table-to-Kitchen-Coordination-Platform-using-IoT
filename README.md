==============================================
            A U T O D I N E
==============================================
Smart Restaurant Table-to-Kitchen Coordination
            Platform using IoT
==============================================


>> ABOUT
---------------------------------------------------------------
AutoDine is an ESP32-based IoT platform that enables autonomous
table-side food ordering and real-time coordination between
restaurant tables and the kitchen.

Customers place orders directly from a table-mounted embedded
unit, while kitchen staff monitor and manage orders through a
browser-based interface hosted on an ESP32.


>> SYSTEM OVERVIEW
---------------------------------------------------------------
AutoDine follows a distributed embedded architecture using
two ESP32 nodes connected over Wi-Fi.

[ TABLE UNIT - ESP32 ]
  - OLED display for menu and status
  - Push buttons for item selection
  - Sends orders wirelessly

[ HOST UNIT - ESP32 ]
  - Embedded web server
  - Kitchen coordination dashboard
  - Sends order status updates


>> KEY FEATURES
---------------------------------------------------------------
 [+] Autonomous table-side ordering
 [+] Dual ESP32 distributed architecture
 [+] OLED-based menu and order status display
 [+] Button-driven user interaction
 [+] Wi-Fi enabled IoT communication
 [+] Embedded web dashboard
 [+] Real-time table-to-kitchen coordination
 [+] Modular and scalable design


>> SYSTEM ARCHITECTURE
---------------------------------------------------------------
+--------------------+        Wi-Fi        +--------------------+
|   TABLE ESP32      |  --------------->  |    HOST ESP32      |
|  OLED + Buttons    |                    |  Web Server        |
+--------------------+                    +--------------------+
                                                  |
                                                  |
                                            Browser Access


>> REPOSITORY STRUCTURE
---------------------------------------------------------------
/Restaurant_Menu
  -> Table-side ESP32 firmware

/Restaurant_Host
  -> Kitchen-side ESP32 web server firmware

/Demo_V1.0.mp4
  -> Working prototype demonstration

/README
  -> Project documentation


>> WORKING FLOW
---------------------------------------------------------------
 1) Menu displayed on OLED screen
 2) Customer selects items using buttons
 3) Order sent to host ESP32 via Wi-Fi
 4) Kitchen views order on web dashboard
 5) Order status updated by kitchen
 6) Status reflected back on table OLED


>> TECHNOLOGIES USED
---------------------------------------------------------------
 Hardware      : ESP32 DevKit V1, SSD1306 OLED, Push Buttons
 Firmware      : ESP-IDF (Embedded C)
 Communication : Wi-Fi, HTTP, JSON
 Interface     : Embedded Web Server (HTML/CSS)


>> APPLICATIONS
---------------------------------------------------------------
 - Smart restaurants and cafés
 - Self-ordering dining systems
 - Embedded IoT demonstrations
 - Academic & final-year projects


>> FUTURE ENHANCEMENTS
---------------------------------------------------------------
 [ ] Touchscreen-based ordering interface
 [ ] Multi-table scalability
 [ ] Cloud connectivity and analytics
 [ ] Mobile application integration


>> LICENSE
---------------------------------------------------------------
 Open-source project for educational and research use


>> AUTHOR NOTE
---------------------------------------------------------------
 AutoDine is developed as an embedded IoT learning project
 demonstrating real-world table-to-kitchen coordination
 using ESP32 and ESP-IDF.

===============================================================
 END OF README
===============================================================
