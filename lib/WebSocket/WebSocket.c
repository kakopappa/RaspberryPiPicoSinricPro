/*===========================================================================*/
/*                                                                           */
/*  Web Socket Interface for the Raspberry Pi Pico W                         */
/*                                                                           */
/*  This is based the the Raspberry Pi Pico "examples".                      */
/*  Only supports simple text type messages.                                 */
/*                                                                           */
/*  Original author: Russell Rhodes, https://github.com/RussellRhodes        */
/*                                                                           */
/*  This is free and unencumbered software released into the public domain.  */
/*  Orignal release date: January 2026                                       */
/*                                                                           */
/*===========================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"

#include "WebSocket.h"

#ifdef WIZNET_BOARD
    #include "wizchip_conf.h"
    #include "wizchip_spi.h"
    #include "httpClient.h"

    #define err_t       int8_t
    #define ERR_OK      0
    #define ERR_ABRT    -13

    #define ENABLE          true
    #define DISABLE         false
    #define HTTP_PORT       80
#else 
    #include "pico/cyw43_arch.h"
    #include "lwip/pbuf.h"
    #include "lwip/tcp.h"
#endif

#define PING_TIMEOUT    (300*1000)

/*  Web Socket Frame layout
 *
 *    0                   1                   2                   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-------+-+-------------+-------------------------------+
 *   |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 *   |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 *   |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 *   | |1|2|3|       |K|             |                               |
 *   +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 *   |     Extended payload length continued, if payload len == 127  |
 *   + - - - - - - - - - - - - - - - +-------------------------------+
 *   |                               |Masking-key, if MASK set to 1  |
 *   +-------------------------------+-------------------------------+
 *   | Masking-key (continued)       |          Payload Data         |
 *   +-------------------------------- - - - - - - - - - - - - - - - +
 *   :                     Payload Data continued ...                :
 *   + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 *   |                     Payload Data continued ...                |
 *   +---------------------------------------------------------------+
 */

typedef struct {
   union {
       struct {
              uint8_t PAYLOADLEN:7;
              uint8_t MASK:1;
              uint8_t OPCODE:4;
              uint8_t RSV:3;
              uint8_t FIN:1;
       } bits;
       struct {
        uint8_t byte1;
        uint8_t byte0;
       } bytes;
   } meta;
   uint8_t  start;
   uint64_t length;
   union {
       uint32_t maskKey;
    uint8_t  maskBytes[4];
   } mask;
} WebsocketPacketHeader_t;

enum WebSocketOpCode {
    WEBSOCKET_OPCODE_CONTINUE = 0x0,
    WEBSOCKET_OPCODE_TEXT = 0x1,
    WEBSOCKET_OPCODE_BIN = 0x2,
    WEBSOCKET_OPCODE_CLOSE = 0x8,
    WEBSOCKET_OPCODE_PING = 0x9,
    WEBSOCKET_OPCODE_PONG = 0xA
};

typedef struct WebSocketClient_s {
#ifdef WIZNET_BOARD
        uint8_t send_buf[BUF_SIZE];
        uint8_t recv_buf[BUF_SIZE];
        char *remote_addr;
        char *hostname;
        uint16_t remote_port;
        int connected;
        bool upgraded;
        char *additional_headers;
        wsMessagehandler messageHandler;
        bool auto_reconnect;
        uint32_t lastPing;
#else
        struct tcp_pcb *tcp_pcb;
        ip_addr_t remote_addr;
        char *hostname;
        uint8_t en[BUF_SIZE];
        uint8_t rx_buffer[BUF_SIZE];
        int buffer_len;
        int rx_buffer_len;
        int sent_len;
        int connected;
        u16_t remote_port;
        bool upgraded;
        char *additional_headers;
        wsMessagehandler messageHandler;
        bool auto_reconnect;
        uint32_t lastPing;
#endif
} WebSocketClient_t;

static uint64_t wsBuildPacket(char* buffer, uint64_t bufferLen, enum WebSocketOpCode opcode, char* payload, uint64_t payloadLen, int mask) 
{
    WebsocketPacketHeader_t header;

    int payloadIndex = 2;
    
    // Fill in meta.bits
    header.meta.bits.FIN = 1;
    header.meta.bits.RSV = 0;
    header.meta.bits.OPCODE = opcode;
    header.meta.bits.MASK = mask;

    // Calculate length
    if(payloadLen < 126) {
        header.meta.bits.PAYLOADLEN = payloadLen;
    } else if(payloadLen < 0x10000) {
        header.meta.bits.PAYLOADLEN = 126;
    } else {
        header.meta.bits.PAYLOADLEN = 127;
    }

    buffer[0] = header.meta.bytes.byte0;
    buffer[1] = header.meta.bytes.byte1;

    // Generate mask
    header.mask.maskKey = (uint32_t)rand();

    // Fill in payload length
    if(header.meta.bits.PAYLOADLEN == 126 && buffer!=NULL) {
            buffer[2] = (payloadLen >> 8) & 0xFF;
            buffer[3] = payloadLen & 0xFF;
        payloadIndex = 4;
     }

    if(header.meta.bits.PAYLOADLEN == 127 && buffer!=NULL) {
         buffer[2] = (payloadLen >> 56) & 0xFF;
         buffer[3] = (payloadLen >> 48) & 0xFF;
         buffer[4] = (payloadLen >> 40) & 0xFF;
         buffer[5] = (payloadLen >> 32) & 0xFF;
         buffer[6] = (payloadLen >> 24) & 0xFF;
         buffer[7] = (payloadLen >> 16) & 0xFF;
         buffer[8] = (payloadLen >> 8)  & 0xFF;
         buffer[9] = payloadLen & 0xFF;
        payloadIndex = 10;
    }

    // Insert masking key
    if(header.meta.bits.MASK && buffer!=NULL) {
        buffer[payloadIndex] = header.mask.maskBytes[0];
        buffer[payloadIndex + 1] = header.mask.maskBytes[1];
        buffer[payloadIndex + 2] = header.mask.maskBytes[2];
        buffer[payloadIndex + 3] = header.mask.maskBytes[3];
        payloadIndex += 4;
    }

    // Ensure the buffer can handle the packet
    if((payloadLen + payloadIndex) > bufferLen) {
        printf("WEBSOCKET BUFFER OVERFLOW \r\n");
        return 1;
    }

    // Copy in payload
    if ( buffer!=NULL ) {
        // memcpy(buffer + payloadIndex, payload, payloadLen);
        for(int i = 0; i < payloadLen; i++) {
            // mask bytes if required
            if(header.meta.bits.MASK) {
                buffer[payloadIndex + i] = payload[i] ^ header.mask.maskBytes[i%4];
            } else {
                buffer[payloadIndex + i] = payload[i];
            }
        }
    }

    return (payloadIndex + payloadLen);

}

static bool wsParsePacket(WebsocketPacketHeader_t *header, char* buffer, uint32_t len)
{
    if ( header != NULL && buffer!= NULL && len>=2 ) {

        memset( (void*)header, 0, sizeof(*header) );

        header->meta.bytes.byte0 = (uint8_t) buffer[0];
        header->meta.bytes.byte1 = (uint8_t) buffer[1];

        // Payload length
        int payloadIndex = 2;
        header->length = header->meta.bits.PAYLOADLEN;

        if(header->meta.bits.PAYLOADLEN == 126) {
            header->length = buffer[2] << 8 | buffer[3];
            payloadIndex = 4;
        }
        
        if(header->meta.bits.PAYLOADLEN == 127) {
            header->length = buffer[6] << 24 | buffer[7] << 16 | buffer[8] << 8 | buffer[9];
            payloadIndex = 10;
        }

        // Mask
        if(header->meta.bits.MASK) {
            header->mask.maskBytes[0] = buffer[payloadIndex + 0];
            header->mask.maskBytes[1] = buffer[payloadIndex + 1];
            header->mask.maskBytes[2] = buffer[payloadIndex + 2];
            header->mask.maskBytes[3] = buffer[payloadIndex + 3];
            payloadIndex = payloadIndex + 4;    
            
            // Decrypt    
            for(uint64_t i = 0; i < header->length; i++) {
                    buffer[payloadIndex + i] = buffer[payloadIndex + i] ^ header->mask.maskBytes[i%4];
            }
        }

        // Payload start
        header->start = payloadIndex;

        return true;
    } else {
        return false;
    }

}

#ifndef WIZNET_BOARD

static err_t wsSent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    return ERR_OK;
}

#endif

bool wsSendOpCode( WebSocketClient_p client, enum WebSocketOpCode opCode )
{
    WebSocketClient_t *state = (WebSocketClient_t *)client;
    bool result = false;

    #ifdef WIZNET_BOARD
    int buffer_len = wsBuildPacket((char *)state->send_buf, BUF_SIZE, opCode, NULL, 0, 1);
    result = ( httpc_send_body(state->send_buf, buffer_len) == buffer_len );
    #else
    state->buffer_len = wsBuildPacket((char *)state->en, BUF_SIZE, opCode, NULL, 0, 1);
    result = ( tcp_write(state->tcp_pcb, state->en, state->buffer_len, TCP_WRITE_FLAG_COPY) == ERR_OK );
    #endif

    return result;
}

#ifdef WIZNET_BOARD
static err_t wsConnected(void *arg, err_t err)
#else
static err_t wsConnected(void *arg, struct tcp_pcb *tpcb, err_t err)
#endif
{
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    if (err != ERR_OK) {
        printf("WebSocket connect failed %d\n", err);
        return ERR_ABRT;
    }
 
    uint32_t now = to_ms_since_boot(get_absolute_time());
    state->upgraded = false;
    state->lastPing = now;

    printf("Requesting WebSocket upgrade...\n");
    // Write HTTP GET Request with Websocket upgrade
    #ifdef WIZNET_BOARD
    uint8_t *buffer = state->send_buf;
    #else
    uint8_t *buffer = state->en;
    #endif
    // Use hostname for Host header if available, otherwise use IP
    const char *host_header = state->hostname ? state->hostname :
    #ifdef WIZNET_BOARD
        state->remote_addr;
    #else
        ip4addr_ntoa(&state->remote_addr);
    #endif
    int len = sprintf( (char *)buffer,
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s\r\n",
        host_header, state->remote_port,
        state->additional_headers?state->additional_headers:"");

    #ifdef WIZNET_BOARD
    //printf("[%.*s](%d)\n",len,state->send_buf,len);
    // Send HTTP requset as message body
    httpc_send_body(buffer, len); 
    #else
    state->buffer_len = len;
    //printf("[%.*s]\n",state->buffer_len,state->en);
    err = tcp_write(state->tcp_pcb, state->en, state->buffer_len, TCP_WRITE_FLAG_COPY);
    #endif 

    state->connected = TCP_CONNECTED;
    return ERR_OK;
}

#ifndef WIZNET_BOARD

static err_t wsPoll(void *arg, struct tcp_pcb *tpcb) {
    return ERR_OK;
}

#endif

static err_t wsClose(void *arg) {
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    err_t err = ERR_ABRT;

    #ifdef WIZNET_BOARD
    httpc_disconnect();
    #else
    if (state->tcp_pcb != NULL) {
        tcp_arg(state->tcp_pcb, NULL);
        tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        err = tcp_close(state->tcp_pcb);
        if (err != ERR_OK) {
            printf("WebSocket close failed %d, calling abort\n", err);
            tcp_abort(state->tcp_pcb);
            err = ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    #endif

    state->connected = TCP_DISCONNECTED;
    state->upgraded = false;

    if ( state->auto_reconnect ) {
        // Reconnect
        wsConnect( (WebSocketClient_p)state );
    }

    return err;
}

#ifndef WIZNET_BOARD

static void wsError(void *arg, err_t err) {
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    state->connected = TCP_DISCONNECTED;
    state->upgraded = false;
    if (err != ERR_ABRT) {
        printf("WebSocket Client Error %d\n", err);
    } else {
        printf("WebSocket Client Error abort %d\n", err);
    }

    wsClose( state );
}

#endif

#ifdef WIZNET_BOARD
err_t wsReceive(void *arg, err_t err) 
#else
err_t wsReceive(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) 
#endif
{
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    bool reply_pong = false;
    uint8_t *buffer = NULL;
    int buffer_len = 0;

#ifndef WIZNET_BOARD
    if (!p) {
        // return tcp_result(arg, -1);
    }
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    state->rx_buffer_len = 0;

    if(p == NULL) {
        // Close
        wsClose(arg);
        return ERR_OK;
    }

    if (p->tot_len > 0) {
        for (struct pbuf *q = p; q != NULL; q = q->next) {
            if((state->rx_buffer_len + q->len) < BUF_SIZE) {
                memcpy(state->rx_buffer + state->rx_buffer_len, (uint8_t *)q->payload, q->len );
                state->rx_buffer_len += q->len;
            }
        }

        buffer_len = state->rx_buffer_len;
        buffer = state->rx_buffer;
#else 
        buffer_len = httpc_recv(state->recv_buf, httpc_isReceived);
        buffer = state->recv_buf;
#endif        

        if ( state->upgraded  ) {
            WebsocketPacketHeader_t header;
            wsParsePacket(&header, (char *)buffer, buffer_len);

            switch( header.meta.bits.OPCODE ) {
                case WEBSOCKET_OPCODE_PING:
                    // send pong
                    uint32_t now = to_ms_since_boot(get_absolute_time());
                    printf("WebSocket received PING (@ %d s)\n", (int)(now-state->lastPing)/1000);
                    state->lastPing = now;
                    reply_pong = true;
                    break;
                case WEBSOCKET_OPCODE_CLOSE:
                    // close connection
                    int64_t error = -1;
                    if ( header.length == 2 ) {
                        error = ((uint8_t *)buffer + header.start)[0]<<8 | ((uint8_t *)buffer + header.start)[1];
                    }
                    printf("WebSocket connection close (%lld)\n",error);
                    break;
                case WEBSOCKET_OPCODE_TEXT:
                    printf("WebSocket received data (Op Code %d)\n",header.meta.bits.OPCODE);
                    buffer += header.start;
                    buffer_len = header.length;
                    break;
                case WEBSOCKET_OPCODE_BIN:
                case WEBSOCKET_OPCODE_CONTINUE:
                default:
                    printf("WebSocket eceived unknown or unsupported data (Op Code %d)\n",header.meta.bits.OPCODE);
                    break;
            }
            if ( header.meta.bits.OPCODE==WEBSOCKET_OPCODE_TEXT && buffer_len>0 ) {
                //printf("tcp_recved [%.*s]\n",buffer_len,buffer);
                if ( buffer_len>=2 && buffer[0]=='{' ) {
                    if ( state->messageHandler ) {
                        if ( buffer_len>=BUF_SIZE ) {
                            buffer_len=BUF_SIZE-1;
                        }
                        buffer[buffer_len] = 0;
                        (state->messageHandler)( (WebSocketClient_p)state, (char *)buffer, buffer_len);
                    }
                }
            }
        } else {
            if ( strnstr((char *)buffer, "HTTP/1.1 101", buffer_len) != NULL &&
                    strnstr((char *)buffer, "Connection: upgrade", buffer_len) != NULL &&
                        strnstr((char *)buffer, "Upgrade: websocket", buffer_len) != NULL ) {
                //printf("tcp_recved [%.*s]\n",buffer_len,buffer);
                printf("WebSocket upgrade acknowladged\n");
                state->upgraded = true;
            }            
        }

#ifndef WIZNET_BOARD        
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);
#endif

    if ( reply_pong ) {
        // send pong
        if ( wsSendOpCode( arg, WEBSOCKET_OPCODE_PONG ) ) {
            printf("WebSocket sent PONG (%d)\n", buffer_len);
        } else {    
            printf("WebSocket PONG send failed!\n");
        }
    }

    return ERR_OK;
}

#ifndef WIZNET_BOARD

static err_t wsTCPConnect(void *arg) 
{
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    err_t err = ERR_ABRT;

    if(state->connected != TCP_DISCONNECTED) return ERR_OK;

    state->tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(&state->remote_addr));
    if (!state->tcp_pcb) {
        return ERR_ABRT;
    }

    tcp_arg(state->tcp_pcb, state);
    tcp_poll(state->tcp_pcb, wsPoll, 1);
    tcp_sent(state->tcp_pcb, wsSent);
    tcp_recv(state->tcp_pcb, wsReceive);
    tcp_err(state->tcp_pcb, wsError);

    state->buffer_len = 0;
    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();
    err = tcp_connect(state->tcp_pcb, &state->remote_addr, state->remote_port, wsConnected);
    cyw43_arch_lwip_end();
    state->connected = TCP_CONNECTING;
    state->upgraded = false;

    return err;
}

#endif

//===============================================================================================================

/*! \brief Initialises a WebSocket for connection
 *  \ingroup Websocket.c
 *
 * \param server ip address of target server
 * \param hostname hostname of target server (for Host header), can be NULL
 * \param port port on which to connect
 * \param messageHandler callback to handle received messages
 * \param additioonalHeaders additional headers to include in the connect
 * \param autoReconnect automatically reconnect if the connection is closed
 * \return handle to a WebSocket client
 */
WebSocketClient_p wsCreate( const char *server, const char *hostname, uint16_t port, wsMessagehandler messageHandler, char *additionalHeaders, bool autoReconnect )
{
    WebSocketClient_t *state = (WebSocketClient_t *)calloc(1, sizeof(WebSocketClient_t));
    if (!state) {
        printf("Failed to allocate WebSocket client\n");
        return NULL;
    }

    #ifdef WIZNET_BOARD
        state->remote_addr = strdup( server );
    #else
        ip4addr_aton(server, &state->remote_addr);
    #endif
    state->hostname = hostname ? strdup(hostname) : NULL;
    state->remote_port = port;
    state->messageHandler = messageHandler;
    if ( additionalHeaders ) {
        state->additional_headers = strdup(additionalHeaders);
    }
    state->auto_reconnect = autoReconnect;

    return( (WebSocketClient_p)state );
}

/*! \brief Connects a client to the server
 *  \ingroup Websocket.c
 *
 * \param client handle of client to connect
 * \return true if succesful
 */
bool wsConnect( WebSocketClient_p client )
{
    bool result = false;
    WebSocketClient_t *state = (WebSocketClient_t *)client;
    #ifdef WIZNET_BOARD
        uint8_t server_ip[4];
        //printf("WebSocket parsing server ip address [%s]\n",state->remote_addr);
        if ( sscanf(state->remote_addr, "%hhu.%hhu.%hhu.%hhu", &server_ip[0], &server_ip[1], &server_ip[2], &server_ip[3]) == 4 ) {
            //printf("WebSocket parsed server ip address [%d][%d][%d][%d]\n", server_ip[0], server_ip[1], server_ip[2], server_ip[3]);
            if ( httpc_init(0, server_ip, state->remote_port, state->send_buf, state->recv_buf) == HTTPC_TRUE) {
                printf("WebSocket connecting to %s:%u\n", state->remote_addr, state->remote_port);
                result = true;
            } else {
                printf("WebSocket HTTP Client initialise failed\n");
            }
        } else {
            printf("WebSocket invalid server ip address [%s]",state->remote_addr);
        }
    #else
        printf("WebSocket connecting to %s:%u\n", ip4addr_ntoa(&state->remote_addr), state->remote_port);
        result = ( wsTCPConnect(state)==ERR_OK );
    #endif
    return result;
}

/*! \brief Destroys a client freeing resourcxes
 *  \ingroup Websocket.c
 *
 * \param client handle of client to connect
 * \return true if succesful
 */
bool wsDestroy( WebSocketClient_p client )
{
    WebSocketClient_t *state = (WebSocketClient_t *)client;
    state->auto_reconnect = false;
    wsClose( state );
    #ifdef WIZNET_BOARD
    if ( state->remote_addr ) {
        free(state->remote_addr);
    }
    #endif
    if ( state->hostname ) {
        free(state->hostname);
    }
    if ( state->additional_headers ) {
        free(state->additional_headers);
    }
    if ( client ) {
        free( client );
    }

    return true;
}

/*! \brief Returns the current connection state
 *  \ingroup Websocket.c
 *
 * \param client handle of client to connect
 * \return connection state TCP_DISCONNECTED, TCP_CONNECTING, or TCP_CONNECTED
 */
int wsConnectState( WebSocketClient_p client )
{
    WebSocketClient_t *state = (WebSocketClient_t *)client;
    return(state->connected);
}

/*! \brief Send a message from the client to the connected server
 *  \ingroup Websocket.c
 *
 * \param client handle of client to connect
 * \param text message to send
 * \param len length of the message
 * \return true if succesful
 */
bool wsSendMessage( WebSocketClient_p client, char *text, size_t len )
{
    WebSocketClient_t *state = (WebSocketClient_t *)client;
    bool result = false;

    #ifdef WIZNET_BOARD
    //printf("send message [%.*s](%d)\n",len,text,len);
    int buffer_len = wsBuildPacket((char *)state->send_buf, BUF_SIZE, WEBSOCKET_OPCODE_TEXT, text, len, 1);
    result = ( httpc_send_body(state->send_buf, buffer_len) == buffer_len );
    #else
    state->buffer_len = wsBuildPacket((char *)state->en, BUF_SIZE, WEBSOCKET_OPCODE_TEXT, text, len, 1);
    result = ( tcp_write(state->tcp_pcb, state->en, state->buffer_len, TCP_WRITE_FLAG_COPY) == ERR_OK );
    #endif

    return result;
}

/*! \brief Handles any WebSocket functionality, must be called periodically
 *  \ingroup Websocket.c
 *
 * \param Nothing
 * \return Nothing
 */
void wsHandler( WebSocketClient_p client )
{
    WebSocketClient_t *state = (WebSocketClient_t *)client;

    #ifdef WIZNET_BOARD
    httpc_connection_handler();

    if( state->connected == TCP_CONNECTED && httpc_isConnected ) {
        // Recv: HTTP response
        if(httpc_isReceived > 0) {
            wsReceive(client, ERR_OK);
        } else {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if ( (now-state->lastPing) > PING_TIMEOUT ) {
                printf("WebSocket PING timeout (%d s), closing socket\n", (int)(now-state->lastPing)/1000);
                wsClose( client );
            }
        }
    } else if ( state->connected == TCP_DISCONNECTED && httpc_isSockOpen ) {
        //printf("HTTP Client connect...\n");
        if ( httpc_connect()==HTTPC_TRUE ) {
            //printf("HTTP Client connecting...\n");
            state->connected = TCP_CONNECTING;
            state->upgraded = false;
        } else {
            printf("WebSocket HTTP Client failed to connect\n");
        }
    } else if ( state->connected == TCP_CONNECTING && httpc_isConnected ) {
        //printf("HTTP Client connected\n");
        wsConnected( client, ERR_OK );
    } else  if( state->connected == TCP_CONNECTED && !httpc_isConnected ) {
        printf("WebSocket not connected, closing socket\n");
        wsClose( client );
    }

    #endif
}

