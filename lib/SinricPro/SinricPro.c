/*===========================================================================*/
/*                                                                           */
/*  Sinric Pro Device Interface for the Raspberry Pi Pico W                  */
/*                                                                           */
/*  This allows a Raspberry Pi Pico W to act as Sinric Pro devce             */
/*  (see https://sinric.pro/), it allows the Pico to act as a number of      */
/*  different device types, dependant on the "actions" supported by the      */
/*  device, this can be extended by adding action-value combinations as      */
/*  required to the "actions" array. For more information on actions see     */
/*  https://github.com/sinricpro/sample_messages.                            */
/*                                                                           */
/*  It does not support actions with more than one "value" field.            */
/*                                                                           */
/*  Original author: Russell Rhodes, https://github.com/RussellRhodes        */
/*                                                                           */
/*  This is free and unencumbered software released into the public domain.  */
/*  Orignal release date: January 2026                                       */
/*                                                                           */
/*===========================================================================*/

#include <string.h>
#include <math.h>
#include <time.h>

#include "pico/stdlib.h"

#include "WebSocket.h"
#include "hmac_sha256.h"
#include "base64.h"
#include "SinricPro.h"

static const char *SinricProAppSecret = NULL;

typedef struct SinricProAction_s {
    char *deviceAction;
    char *deviceValueName;
    jsonType_t deviceValueDataType;

} SinricProAction_t;

// add action, value combinations as required...
SinricProAction_t actions[] = {
    { "setPowerState", "state", JSON_TEXT },
    { "setPowerLevel", "powerLevel", JSON_INTEGER },
    { "adjustPowerLevel", "powerLevel", JSON_INTEGER },
    { "setBrightness", "brightness", JSON_INTEGER },
    { "adjustBrightness", "brightnessDelta", JSON_INTEGER },
    { "DoorbellPress", "state", JSON_INTEGER },
    { "targetTemperature", "temperature", JSON_INTEGER },
    { "adjustTargetTemperature", "temperature", JSON_INTEGER },
    { "currentTemperature", "temperature", JSON_INTEGER },
    { "setMode", "mode", JSON_TEXT },
};

#define NUM_ACTIONS (sizeof(actions)/sizeof(SinricProAction_t))

SinrecProDeviceActionHandler_t userDefinedActionHandler = NULL;

static WebSocketClient_p wsClient = NULL;

static int64_t timestamp = 0;
static int64_t timestampSecsBoot = 0;

static bool defaultActionHandler( char *deviceId, char *action, jsonValue_t value, jsonType_t dataType )
{
    switch( dataType ) {
        case JSON_TEXT:
            printf("Device[%s] %s=[%s]\n",deviceId,action,value.text);
            break;
        case JSON_INTEGER:
            printf("Device[%s] %s=[%lld]\n",deviceId,action,value.integer);
            break;
        case JSON_REAL:
            printf("Device[%s] %s=[%.2f]\n",deviceId,action,value.real);
            break;
        case JSON_BOOLEAN:
            printf("Device[%s] %s=[%s]\n",deviceId,action,value.boolean?"true":"false");
            break;
        default:
            printf("Device[%s] %s=[dataType %d not handled]\n",deviceId,action,dataType);
            break;
    }

    return true;
}

static char *getSignature( char *payload )
{
    #define SHA256_HASH_SIZE 32
    static char signature[(SHA256_HASH_SIZE/3)*4+4+1];
    uint8_t out[SHA256_HASH_SIZE];

    hmac_sha256( SinricProAppSecret, strlen(SinricProAppSecret), payload, strlen(payload), &out, sizeof(out) );
    base64_encode( (char *)out, SHA256_HASH_SIZE, signature );

    //printf("signature=[%s](%d)\n",signature,strlen(signature));

    if ( strlen(signature) > (SHA256_HASH_SIZE/3)*4+4 ) {
        printf("SIGNATURE BUFFER OVERFLOW !!!\n");
    }

    return signature;
}

static bool buildJsonPayload( char *action, char *clientId, int64_t createdAt, char *deviceId, char *replyToken, jsonValue_t value, char *valueName, jsonType_t valueType  )
{
    json_put( "action", (jsonValue_t)action, JSON_TEXT );
    json_put( "clientId", (jsonValue_t)clientId, JSON_TEXT );
    json_put( "scope", (jsonValue_t)"device", JSON_TEXT );
    json_put( "createdAt", (jsonValue_t)createdAt, JSON_INTEGER );
    json_put( "deviceId", (jsonValue_t)deviceId, JSON_TEXT );
    json_put( "message", (jsonValue_t)"OK", JSON_TEXT );
    json_put( "replyToken", (jsonValue_t)replyToken, JSON_TEXT );
    json_put( "success", (jsonValue_t)true, JSON_BOOLEAN );
    json_put( "type", (jsonValue_t)"response", JSON_TEXT );
    json_put( "value", (jsonValue_t)NULL, JSON_OBJ );
    json_put( valueName, (jsonValue_t)value, valueType );
    json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );

    return true;
}

static void handleWSmessage( WebSocketClient_p client,  char *msg, int len )
{
    bool unknown = true;
    jsonValue_t data;
    char *deviceId = NULL;
    char *clientId = NULL;
    char *replyToken = NULL;
    char *action = NULL;
    char *value_text = NULL;
    int64_t createdAt = 0;

    printf("Message received\n");

    // if timestamp store and use as base time...
    if  ( json_get( msg, "timestamp", JSON_INTEGER, &data ) ) {
        timestampSecsBoot = to_ms_since_boot(get_absolute_time())/1000;
        timestamp = data.integer;
        printf( "timestamp: '%lld'\n", timestamp );    
        time_t now = SinricProServerTime();
        printf("Current server time is %s",ctime(&now));            
        unknown = false;
    } 
    // if device message parse message for required data...
    else if ( json_get( msg, "deviceId", JSON_TEXT, &data ) ) {
        deviceId = strdup(data.text);
        //printf( "deviceId: '%s'\n", (char *)deviceId );    
        if ( json_get( msg, "clientId", JSON_TEXT, &data ) ) {
            clientId = strdup(data.text);
            //printf( "clientId: '%s'\n", (char *)clientId );    
            if ( json_get( msg, "replyToken", JSON_TEXT, &data ) ) {
                replyToken = strdup(data.text);
                //printf( "replyToken: '%s'\n", (char *)replyToken );    
                if ( json_get( msg, "createdAt", JSON_INTEGER, &data ) ) {
                    createdAt = data.integer;
                    //printf( "createdAt: '%lld'\n", *(int64_t *)createdAt );    
                    if ( json_get( msg, "action", JSON_TEXT, &data ) ) {
                        action = strdup(data.text);
                        //printf( "action: '%s'\n", (char *)action );    

                        int actionNum = 0;
                        for ( actionNum = 0 ; actionNum < NUM_ACTIONS ; actionNum++ ) {
                            if ( strcmp(actions[actionNum].deviceAction, action)==0 ) 
                                break;
                        }

                        if ( actionNum < NUM_ACTIONS && strcmp(actions[actionNum].deviceAction, action)==0 ) { 
                            if ( json_get( msg, actions[actionNum].deviceValueName, actions[actionNum].deviceValueDataType, &data ) ) {
                                jsonValue_t value = data;
                                if ( actions[actionNum].deviceValueDataType == JSON_TEXT ) {
                                    // make a copy
                                    value_text = strdup(value.text);
                                    value.text = value_text;
                                }

                                //printf("[%.*s](%d)\n",len,msg,len);

                                if ( userDefinedActionHandler != NULL ) {
                                    unknown = !userDefinedActionHandler( deviceId, action, value, actions[actionNum].deviceValueDataType );
                                } else {
                                    unknown = !defaultActionHandler( deviceId, action, value, actions[actionNum].deviceValueDataType );
                                }

                                char json_buffer[BUF_SIZE+1];
                                createdAt = SinricProServerTime();

                                // build "payload" so we can create signature...
                                json_put_start( json_buffer, BUF_SIZE );
                                buildJsonPayload( action, clientId, createdAt, deviceId, replyToken, value, actions[actionNum].deviceValueName, actions[actionNum].deviceValueDataType );
                                json_put_end();
                                //printf("payload=[%s](%d)\n",json_buffer,strlen(json_buffer));

                                // create signature...
                                char *signature = getSignature( json_buffer );

                                // build full response...
                                json_put_start( json_buffer, BUF_SIZE );
                                json_put( "header", (jsonValue_t)NULL, JSON_OBJ );         
                                json_put( "payloadVersion", (jsonValue_t)2, JSON_INTEGER );
                                json_put( "signatureVersion", (jsonValue_t)1, JSON_INTEGER );
                                json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );
                                json_put( "payload", (jsonValue_t)NULL, JSON_OBJ );         
                                buildJsonPayload( action, clientId, createdAt, deviceId, replyToken, value, actions[actionNum].deviceValueName, actions[actionNum].deviceValueDataType );
                                json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );
                                json_put( "signature", (jsonValue_t)NULL, JSON_OBJ );
                                json_put( "HMAC", (jsonValue_t)signature, JSON_TEXT );
                                json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );
                                json_put_end();

                                //printf("Response\n[%s](%d)\n",json_buffer,strlen(json_buffer));

                                // send response...
                                if ( wsSendMessage( client, json_buffer, strlen(json_buffer) ) ) {
                                    printf("Response sent\n");
                                } else {    
                                    printf("Failed to send response\n");
                                }
                            } else {
                                printf("Data [%s] not found\n",actions[actionNum].deviceValueName);
                            }
                        } else {
                            printf("Unexpected action [%s]\n",action);
                        }
                    } 
                }
            } 
        }
    }

    // if we don't recognise the message print it out...
    if ( unknown ) {
        printf("Message unknown or invalid\n[%.*s](%d)\n",len,msg,len);
    }

    // free resources...
    if ( deviceId ) free(deviceId);
    if ( clientId ) free(clientId);
    if ( replyToken ) free(replyToken);
    if ( action ) free(action);
    if ( value_text ) free(value_text);
}

static bool buildNotifyPayload( char *action, char *causeText, int64_t createdAt, char *deviceId, char *replyToken, jsonValue_t value, char *valueName, jsonType_t valueType  )
{
    json_put( "action", (jsonValue_t)action, JSON_TEXT );
    json_put( "cause", (jsonValue_t)NULL, JSON_OBJ );
    json_put( "type", (jsonValue_t)causeText, JSON_TEXT );
    json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );
    //json_put( "scope", (jsonValue_t)"device", JSON_TEXT );
    json_put( "createdAt", (jsonValue_t)createdAt, JSON_INTEGER );
    json_put( "deviceId", (jsonValue_t)deviceId, JSON_TEXT );
    json_put( "replyToken", (jsonValue_t)replyToken, JSON_TEXT );
    json_put( "type", (jsonValue_t)"event", JSON_TEXT );
    json_put( "value", (jsonValue_t)NULL, JSON_OBJ );
    json_put( valueName, (jsonValue_t)value, valueType );
    json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );

    return true;
}

//===============================================================================================================

/*! \brief Initialises parameters for connection to Sinric Pro
 *  \ingroup SinricPro.c
 *
 * \param server_ip ip address of Sinric Pro server
 * \param hostname hostname of Sinric Pro server
 * \param port TCP/IP port to conneced to Sinric Pro server on
 * \param appKey APP_KEY as assigned by Sinric Pro
 * \param appSecret APP_SECRET as assigned by Sinric Pro
 * \param deviceIDs deviceIDs as assigned by Sinric Pro
 * \param firmwareVersion version number of this App
 * \param localIPAddress local IP address
 * \param localMACAddress local MAC address
 * \return true if succesful
 */
bool SinricProInit(const char *server, const char *hostname, uint16_t port, const char *appKey, const char *appSecret, const char*deviceIDs, const char *firmwareVersion, const char *localIPAddress, const char *localMACAddress )
{
    // save apo secret
    SinricProAppSecret = appSecret;

    const char *ip_address = strdup(localIPAddress);
    const char *mac_address = strdup(localMACAddress);

    printf("ip address=[%s]\n", ip_address);
    printf("mac address=[%s]\n",mac_address);

    // create additional web socket headers for Sinric Pro...
    char additional_headers[300];
    sprintf(additional_headers,
        "appkey: %s\r\n"
        "deviceids: %s\r\n"
        "restoredevicestates: true\r\n"
        "platform: Raspberry Pi Pico\r\n"
        "mac: %s\r\n"
        //"SDKVersion: '4.0.0'\r\n"
        "ip: %s\r\n"
        "firmwareVersion: %s\r\n",
        appKey, deviceIDs,
        mac_address, ip_address,
        firmwareVersion );

    // create WebSocket client and connect...
    wsClient = wsCreate( server, hostname, port, handleWSmessage, additional_headers, true );

    return( wsClient != NULL );
}

/*! \brief Connects to Sinric Pro
 *  \ingroup SinricPro.c
 *
 * \param actionHandler callback to handle actions generated by Sinric Pro
 * \return true if succesful
 */
bool SinricProConnect( SinrecProDeviceActionHandler_t actionHandler )
{
    userDefinedActionHandler = actionHandler;
    return( wsConnect( wsClient ) );
}

/*! \brief Notifys Sinric Pro of a data update
 *  \ingroup SinricPro.c
 *
 * \param deviceId device that needs updating
 * \param action action to be updated
 * \param cause why the update has happened ( PHYSICAL_INTERACTION, PERIODIC_POL )
 * \param name of the value to update
 * \param value the new value
 * \param valueType the datatype of the value
 * \return true if succesful
 */
bool SinricProNotify( char *deviceId, char *action, SinricProCause_t cause, char *valueName, jsonValue_t value, jsonType_t valueType )
{
    bool result = false;
    char json_buffer[BUF_SIZE+1];
    int64_t createdAt = SinricProServerTime();
    char *causeText = cause==PHYSICAL_INTERACTION?"PHYSICAL_INTERACTION":cause==PERIODIC_POLL?"PERIODIC_POLL":"UNKNOWN CAUSE";

    // build "payload" so we can create signature...
    json_put_start( json_buffer, BUF_SIZE );
    buildNotifyPayload( action, causeText, createdAt, deviceId, deviceId, value, valueName, valueType );
    json_put_end();

    //printf("Notify payload=[%s](%d)\n",json_buffer,strlen(json_buffer));

    // create signature...
    char *signature = getSignature( json_buffer );

    // build full response...
    json_put_start( json_buffer, BUF_SIZE );
    json_put( "header", (jsonValue_t)NULL, JSON_OBJ );         
    json_put( "payloadVersion", (jsonValue_t)2, JSON_INTEGER );
    json_put( "signatureVersion", (jsonValue_t)1, JSON_INTEGER );
    json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );
    json_put( "payload", (jsonValue_t)NULL, JSON_OBJ );         
    buildNotifyPayload( action, causeText, createdAt, deviceId, deviceId, value, valueName, valueType );
    json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );
    json_put( "signature", (jsonValue_t)NULL, JSON_OBJ );
    json_put( "HMAC", (jsonValue_t)signature, JSON_TEXT );
    json_put( NULL, (jsonValue_t)NULL, JSON_OBJ );
    json_put_end();

    //printf("Notify Request\n[%s](%d)\n",json_buffer,strlen(json_buffer));

    // send request...
    if ( wsSendMessage( wsClient, json_buffer, strlen(json_buffer) ) ) {
        printf("Notify request [%s] sent\n", action);
        result = true;
    } else {    
        printf("Failed to send [%s] notify request\n", action);
    }
    
    return result;
}

/*! \brief Handles any WebSocket functionality, must be called periodically
 *  \ingroup SinricPro.c
 *
 * \param Nothing
 * \return Nothing
 */
void SinricProHandler( void )
{
    wsHandler( wsClient );
}

/*! \brief Gets current time as sent by the Sinric Pro Server
 *  \ingroup SinricPro.c
 *
 * \param None
 * \return true Unix timestamp
 */
int64_t SinricProServerTime( void )
{
    int64_t result = timestamp + ( to_ms_since_boot(get_absolute_time())/1000 - timestampSecsBoot);
    return result;
}

