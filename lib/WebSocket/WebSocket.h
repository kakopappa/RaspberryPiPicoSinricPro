#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define BUF_SIZE 2048

#define TCP_DISCONNECTED 0
#define TCP_CONNECTING   1
#define TCP_CONNECTED    2

typedef void *WebSocketClient_p;
typedef void (*wsMessagehandler)( WebSocketClient_p client, char *message, int len );

WebSocketClient_p wsCreate( const char *server, uint16_t port, wsMessagehandler messageHandler, char *additional_headers, bool autoReconnect );
bool wsConnect( WebSocketClient_p client );
bool wsDestroy( WebSocketClient_p client );
int wsConnectState( WebSocketClient_p client );
bool wsSendMessage( WebSocketClient_p client, char *text, size_t len );
const char *wsGetLocalIPAddress( void );
const char *wsGetLocalMACAddress( void );
void wsHandler( void );

#ifdef __cplusplus
}
#endif
