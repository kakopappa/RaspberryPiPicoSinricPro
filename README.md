# RaspberryPiPicoSinricPro

Sinric Pro "Dimmer Switch" Device Example for the Raspberry Pi Pico W


This gives an example of how the Raspberry Pi Pico can act as a device for SinRic Pro (see https://sinric.pro/), and hence be controlled by Google Home or Alexa commands.

This simple example receives On/Off and Power Level messages from Sinric Pro, and also periodically sends random Power Level notifications. State change notifications can also be triggered by pressing the BOOTSEL button. It is also capable of supporting other device types, e.g. "Switch", "Garage Door", etc.

This example code can easily be modified to handle other Sinric Pro device types, by extending the "actions" array in SinricPro.c

To configure the connection and device, create a config.h file in the root directory and add the following defines:-

        WIFI_SSID       - your WiFi SSID
        WIFI_PASSWORD   - your WiFi password
        APP_KEY         - the APP_KEY from Sinric Pro
        APP_SECRET      - the APP_SECRET from Sinric Pro
        DEVICE_IDS      - the device ID(s) from Sinric Pro

Original author: Russell Rhodes, https://github.com/RussellRhodes    
This is free and unencumbered software released into the public domain.
Orignal release date: January 2026    
