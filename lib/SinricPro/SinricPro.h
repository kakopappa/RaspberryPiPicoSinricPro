#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "json.h"

typedef bool (*SinrecProDeviceActionHandler_t)( char *deviceID, char *action, jsonValue_t value, jsonType_t dataType );
typedef enum SinricProCause_e { PHYSICAL_INTERACTION, PERIODIC_POLL } SinricProCause_t;

bool SinricProInit(const char *server, const char *hostname, uint16_t port, const char *appKey, const char *appSecret, const char*deviceIDs, const char *firmwareVersion, const char *localIPAddress, const char *localMACAddress );
bool SinricProConnect( SinrecProDeviceActionHandler_t actionHandler );
bool SinricProNotify( char *deviceId, char *action, SinricProCause_t cause, char *valueName, jsonValue_t value, jsonType_t valueType );
int64_t SinricProServerTime( void );
void SinricProHandler( void );

#ifdef __cplusplus
}
#endif
