# Raspberry Pi Pico W Sinric Pro Example Device

Sinric Pro "Dimmer Switch" Device Example in C for the Raspberry Pi Pico W.

This gives an example of how the Raspberry Pi Pico W can act as a device for SinRic Pro (see https://sinric.pro/), and hence be controlled by Google Home or Alexa commands. This can be built under VSCode with the Raspberry Pi Pico extension.

This also now supports WIZnet Pico boards with the WIZnet libraries, add "add_definitions(-DWIZNET_BOARD)" to enable.

This simple example receives On/Off and Power Level messages from Sinric Pro, and also periodically sends random Power Level notifications. State change notifications can also be triggered by pressing the BOOTSEL button. It is also capable of supporting other device types, e.g. "Switch", "Garage Door", etc.

To configure the connection and device, create a config.h file in the root directory and add the following defines:-

        WIFI_SSID       - your WiFi SSID
        WIFI_PASSWORD   - your WiFi password
        APP_KEY         - the APP_KEY from Sinric Pro
        APP_SECRET      - the APP_SECRET from Sinric Pro
        DEVICE_IDS      - the device ID(s) from Sinric Pro

For this particular example you will need a "dimmer switch" set up and assign it's ID to DIMMER_ID.

The connection to the Sinric Pro server is initiated by the following code.

        // Initialise Sinric Pro connection parameters
        SinricProInit( server_ip, TCP_PORT, APP_KEY, APP_SECRET, DEVICE_IDS, FIRMWARE_VERSION );
        // Connect to Sinric Pro server and assign message handler
        if ( SinricProConnect( deviceActionHandler ) ) {
            printf("Sinric Pro Connected\n");
        }

Any messages received from Sinric Pro for the configured device are handled by the message handler.

        // device action handler, called when we receive a recognised message from Sinric Pro
        bool deviceActionHandler( char *deviceId, char *action, jsonValue_t value, jsonType_t dataType )
        {
            switch( dataType ) {
                case JSON_TEXT:
                    printf("Device[%s] %s=[%s]\n",deviceId,action,value.text);
                    if ( strcmp(action,"setPowerState")==0 ) {
                        setLed( strcmp(value.text,"On")==0 );
                    }
                    break;
                case JSON_INTEGER:
                    printf("Device[%s] %s=[%lld]\n",deviceId,action,value.integer);
                    if ( strcmp(action,"setPowerLevel")==0 ) {
                        setLed( value.integer>0 );
                    }
                    break;
                case JSON_REAL:
                    printf("Device[%s] %s=[%.2f]\n",deviceId,action,value.real);
                    break;
                case JSON_BOOLEAN:
                    printf("Device[%s] %s=[%s]\n",deviceId,action,value.boolean?"true":"false");
                    break;
            }
        
            return true;
        }

Notifications can also be sent from the device to Sinric Pro.

        // send random power level...
        SinricProNotify( DIMMER_ID, "setPowerLevel", PERIODIC_POLL, "powerLevel", value, JSON_INTEGER );

This example code can easily be modified to handle other Sinric Pro device types, by extending the "actions" array in SinricPro.c

Original author: Russell Rhodes, https://github.com/RussellRhodes    
Orignal release date: January 2026

This is free and unencumbered software released into the public domain.
