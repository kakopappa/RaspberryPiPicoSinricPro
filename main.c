/*===========================================================================*/
/*                                                                           */
/*  Sinric Pro "Dimmer Switch" Device Example for the Raspberry Pi Pico W    */
/*                                                                           */
/*  This gives an example of how the Raspberry Pi Pico can act as a device   */
/*  for SinRic Pro (see https://sinric.pro/), and hence be controlled by     */
/*  Google Home or Alexa commands.                                           */
/*                                                                           */
/*  This simple example receives On/Off and Power Level messages from        */
/*  Sinric Pro, and also periodically sends random Power Level notifications.*/
/*  State change notifications can also be triggered by pressing the BOOTSEL */
/*  button. It is also capable of supporting other device types,             */
/*  e.g. "Switch", "Garage Door", etc                                        */
/*                                                                           */
/*  This example code can easily be modified to handle other Sinric Pro      */
/*  device types, by extending the "actions" array in SinricPro.c            */
/*                                                                           */
/*  To configure the connection and device, create a config.h file in the    */
/*  root directory and add the following defines:-                           */
/*      WIFI_SSID       - your WiFi SSID                                     */
/*      WIFI_PASSWORD   - your WiFi password                                 */
/*      APP_KEY         - the APP_KEY from Sinric Pro                        */
/*      APP_SECRET      - the APP_SECRET from Sinric Pro                     */
/*      DEVICE_IDS      - the device ID(s) from Sinric Pro                   */
/*                                                                           */
/*  Original author: Russell Rhodes, https://github.com/RussellRhodes        */
/*                                                                           */
/*  This is free and unencumbered software released into the public domain.  */
/*  Orignal release date: January 2026                                       */
/*                                                                           */
/*===========================================================================*/

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "hardware/watchdog.h"

#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include <string.h>
#include <math.h>
#include <malloc.h>
#include <time.h>

#include "SinricPro.h"
#include "json.h"
#include "hmac_sha256.h"
#include "base64.h"
#include "dnsclient.h"

#if defined __has_include
#if __has_include ("config.h")
#include "config.h"
#endif
#endif

#define FIRMWARE_VERSION    "0.1.1"

#define SERVER_URL          "ws.sinric.pro"         // Use 'wss://ws.sinric.pro' for secure connection
#define SERVER_IP           "162.55.80.75"          // Sinric Pro
#define TCP_PORT            80                      // 8082

// WiFi connection details...
#ifndef WIFI_SSID
#define WIFI_SSID           "your SSID";
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD       "your password";
#endif

// Sinric Pro defines...
#ifndef APP_KEY
#define APP_KEY             "your app key"      
#endif
#ifndef APP_SECRET
#define APP_SECRET          "your app secret"   
#endif
#ifndef DEVICE_IDS
#define DEVICE_IDS           "your switch id"
#endif

#define DIMMER_ID           "your switch id"

bool powerState = false;
int64_t powerLevel = 100;


uint32_t getTotalHeap(void) {
   extern char __StackLimit, __bss_end__;
   
   return &__StackLimit  - &__bss_end__;
}

uint32_t getFreeHeap(void) {
   struct mallinfo m = mallinfo();

   return getTotalHeap() - m.uordblks;
}

bool __no_inline_not_in_flash_func(isBootSelPresssed)() {
    const uint CS_PIN_INDEX = 1;

    // Must disable interrupts, as interrupt handlers may be in flash, and we
    // are about to temporarily disable flash access!
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Note we can't call into any sleep functions in flash right now
    for (volatile int i = 0; i < 1000; ++i);

    // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
    // Note the button pulls the pin *low* when pressed.
    bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));

    // Need to restore the state of chip select, else we are going to have a
    // bad time when we return to code in flash!
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

int ledInit(void) 
{
#if defined(PICO_DEFAULT_LED_PIN)
    // A device like Pico that uses a GPIO for the LED will define PICO_DEFAULT_LED_PIN
    // so we can use normal GPIO functionality to turn the led on and off
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // For Pico W devices we need to initialise the driver etc
    //return cyw43_arch_init();
    //return cyw43_arch_init_with_country( CYW43_COUNTRY_UK );
    // initialised elsewhere so OK
    return PICO_OK;
#endif
}

void setLed(bool led_on) {
#if defined(PICO_DEFAULT_LED_PIN)
    // Just set the GPIO on or off
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // Ask the wifi "driver" to set the GPIO on or off
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#endif
}

bool getLed(void) {
    bool result = false;
#if defined(PICO_DEFAULT_LED_PIN)
    // Just set the GPIO on or off
    result = gpio_get(PICO_DEFAULT_LED_PIN);
#elif defined(CYW43_WL_GPIO_LED_PIN)
    // Ask the wifi "driver" to set the GPIO on or off
    result = cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN);
#endif
    return result;
}

void softwareReset()
{
    printf("Reset in");
    for ( int i = 5 ; i>0 ; i-- ) { 
        printf(" %d", i);
        sleep_ms( 1000 );
    }
    printf("\r\n");
    watchdog_enable(1, 1);
    while(1);
}

// device action handler, called when we receive a recognised message from Sinric Pro
bool deviceActionHandler( char *deviceId, char *action, jsonValue_t value, jsonType_t dataType )
{
    switch( dataType ) {
        case JSON_TEXT:
            printf("Device[%s] %s=[%s]\n",deviceId,action,value.text);
            if ( strcmp(action,"setPowerState")==0 ) {
                powerState = strcmp(value.text,"On")==0;
            }
            break;
        case JSON_INTEGER:
            printf("Device[%s] %s=[%lld]\n",deviceId,action,value.integer);
            if ( strcmp(action,"setPowerLevel")==0 ) {
                powerLevel = value.integer;
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

const char *getLocalIPAddress( void ) 
{
    static char localIPAddress[16+10];
    ip_addr_t ip_address;

    // get ip address
    memcpy(&ip_address, &cyw43_state.netif[CYW43_ITF_STA].ip_addr, sizeof (ip_addr_t));
    strncpy( localIPAddress, ip4addr_ntoa(&ip_address), 16 );

    return localIPAddress;
}

const char *getLocalMACAddress( void )
{
    static char localMACAddress[18+10];

        // get the mac address
    for ( int i=0 ; i < 6 ; i++ ) {
        sprintf(&localMACAddress[i*3], i<5?"%02X-":"%02X",cyw43_state.mac[i]);
    }

    return localMACAddress;
}

int main() 
{
    int rc = 0;
    stdio_init_all();
    ledInit();

    // initialise pico w
    rc = cyw43_arch_init_with_country(CYW43_COUNTRY_UK);

    printf("Starting in");
    for ( int i = 10 ; i>0 ; i-- ) { 
        printf(" %d", i);
        setLed(i&1);
        sleep_ms( 1000 );
    }
    printf("\r\n");

    if ( rc ) {
        printf("Failed to initialise cyw43 arch\n");
        softwareReset();
    }

    setLed(false);
    printf("Connecting to WiFi...\n");

    // Connect to network
    char ssid[] = WIFI_SSID;
    char pass[] = WIFI_PASSWORD;

    cyw43_arch_enable_sta_mode();

    if (rc=cyw43_arch_wifi_connect_timeout_ms(ssid, pass, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Failed to connect to WiFi (%d)\n", rc);
        switch( rc ) {
            case PICO_ERROR_TIMEOUT:
                printf("Timeout reached before a successful connection\n");
                break;
            case PICO_ERROR_BADAUTH:
                printf("The WiFi password is wrong\n");
                break;
            default:    
            case PICO_ERROR_CONNECT_FAILED:
                printf("The connection failed for some other reason\n");
                break;
        }
        printf("Retrying connection to WiFi\n");
        softwareReset();
    } else {
        printf("Connected to WiFi SSID %s\n", ssid);
    }

    char *server_ip = SERVER_IP;
    ip_addr_t sinricProServer;
    if ( get_dns_address( SERVER_URL, &sinricProServer ) == ERR_OK ) {
        server_ip = strdup(ip4addr_ntoa(&sinricProServer));
        printf( "Server [%s] ip address [%s]\n", SERVER_URL, server_ip );
    } else {
        printf( "Could not find [%s] on DNS\n", SERVER_URL );
    }

    // Initialise Sinric Pro connection parameters
    SinricProInit( server_ip, SERVER_URL, TCP_PORT, APP_KEY, APP_SECRET, DEVICE_IDS, FIRMWARE_VERSION, getLocalIPAddress(), getLocalMACAddress() );
    // Connect to Sinric Pro server and assign message handler
    if ( SinricProConnect( deviceActionHandler ) ) {
        printf("Sinric Pro Connected\n");
    }

    uint32_t keyTimer = to_ms_since_boot(get_absolute_time());
    uint32_t updateTimer = to_ms_since_boot(get_absolute_time());
    while(true) {
        SinricProHandler();

        if (powerState && powerLevel>0) {
            setLed(true);
            sleep_us(powerLevel*100);
        } else {
            setLed(false);
        }

        // Every 250 millisecond
        if((to_ms_since_boot(get_absolute_time()) - keyTimer) > 250) {
            
            static bool bootSelPressed = false;

            if ( !bootSelPressed && isBootSelPresssed() ) {
                if ( isBootSelPresssed() ) {
                    jsonValue_t value;
                    value.text = powerState?"Off":"On";
                    // notify of state change
                    printf("Power State changed to '%s'\n", value.text);
                    if ( SinricProNotify( DIMMER_ID, "setPowerState", PHYSICAL_INTERACTION, "state", value, JSON_TEXT )) {
                        powerState = !powerState;
                    }
                }
            }

            bootSelPressed = isBootSelPresssed();

            keyTimer = to_ms_since_boot(get_absolute_time());
        }
        // Every 5 minute
        if((to_ms_since_boot(get_absolute_time()) - updateTimer) > 300 * 1000) {

            time_t now = SinricProServerTime();
            printf("Server time is %s",ctime(&now));            
            printf("Memory:%dkb free of %dkb\n", getFreeHeap()/1024, getTotalHeap()/1024 );
            
            jsonValue_t value;
            value.integer = get_rand_32()%100 + 1;
            // send random power level...
            printf("Power Level changed to %lld\n", value.integer);
            if ( SinricProNotify( DIMMER_ID, "setPowerLevel", PERIODIC_POLL, "powerLevel", value, JSON_INTEGER ) ) {
                powerLevel = value.integer;
                // Sinric Pro also set the Power State to "on" when setting the Power Level
                if ( powerLevel> 0 ) {
                    powerState = true;
                }
            }

            updateTimer = to_ms_since_boot(get_absolute_time());
        }

        if (powerState && powerLevel<100) {
            setLed(false);
            sleep_us((100-powerLevel)*100);
        }
    } 

    return 0;
}

