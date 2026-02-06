A U T O D I N E

══════════════════════════════════════════════

Smart Restaurant Table to Kitchen Coordination
Platform using IoT

══════════════════════════════════════════════

PROJECT DESCRIPTION

══════════════════════════════════════════════

    AutoDine is an ESP32 based IoT platform that enables

    autonomous table side food ordering and real time

    coordination between restaurant tables and the kitchen.



    The system is designed to reduce manual order taking

    by allowing customers to place orders directly from a

    table mounted embedded unit, while the kitchen manages

    orders through a browser based interface hosted on an

    ESP32 device.



    AutoDine focuses on embedded communication, simplicity

    and real world workflow automation using IoT.



SYSTEM OVERVIEW

══════════════════════════════════════════════

    The project uses a distributed embedded architecture

    with two ESP32 devices connected over WiFi.



    One ESP32 acts as the customer table unit and provides

    menu interaction using an OLED display and push buttons.



    The second ESP32 acts as the host unit and runs an

    embedded web server that can be accessed from any

    laptop or mobile browser by kitchen staff.



SYSTEM ARCHITECTURE

══════════════════════════════════════════════

    TABLE UNIT ESP32



        • OLED display for menu and order status



        • Push buttons for item selection



        • Sends order data over WiFi



    HOST UNIT ESP32



        • Embedded web server



        • Displays incoming orders



        • Allows order status updates



        • Sends status back to table unit



FEATURES

══════════════════════════════════════════════

    • Autonomous table side food ordering



    • Dual ESP32 distributed architecture



    • OLED based menu and status display



    • Button driven customer interaction



    • WiFi enabled IoT communication



    • Browser based kitchen dashboard



    • Real time table to kitchen coordination



    • Modular and scalable embedded design



WORKING PRINCIPLE

══════════════════════════════════════════════

    1. Menu is displayed on the OLED at the table



    2. Customer selects food items using push buttons



    3. Order data is sent to the host ESP32 via WiFi



    4. Host ESP32 displays the order on web dashboard



    5. Kitchen updates order status using browser



    6. Updated status is sent back to table ESP32



    7. Customer sees live order status on OLED display



REPOSITORY CONTENTS

══════════════════════════════════════════════

    Restaurant_Menu



        Contains firmware for the table side ESP32



        including OLED display and button logic



    Restaurant_Host



        Contains firmware for the host ESP32



        including embedded web server code



    Demo_V2.0.mp4



        Demonstration video showing the complete



        working of the AutoDine system



    README



        Project documentation



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



        • Embedded Web Server

        • Browser based dashboard



APPLICATIONS

══════════════════════════════════════════════

    • Smart restaurants and cafes

    • Self ordering dining systems

    • Embedded IoT demonstrations

    • Academic and final year projects



FUTURE SCOPE

══════════════════════════════════════════════

    • Touchscreen based menu interface

    • Multiple table support

    • Cloud connectivity for analytics

    • Mobile application integration

    • Payment and billing extension


PROJECT STATUS

══════════════════════════════════════════════

    Current version demonstrates a fully working

    table to kitchen coordination prototype using

    dual ESP32 devices and IoT communication.



AUTHOR NOTE

══════════════════════════════════════════════

    AutoDine is developed as an embedded IoT learning

    project focused on real world restaurant workflow

    automation using ESP32 and ESP IDF.



══════════════════════════════════════════════

END OF README

══════════════════════════════════════════════
