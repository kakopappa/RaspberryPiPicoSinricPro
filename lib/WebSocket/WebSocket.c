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

#include "pico/cyw43_arch.h"
#include "WebSocket.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

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
    struct tcp_pcb *tcp_pcb;
    ip_addr_t remote_addr;
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
    if (  buffer!=NULL ) {
        // memcpy(buffer + payloadIndex, payload, payloadLen);
        for(int i = 0; i < payloadLen; i++) {
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

static err_t wsSent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    return ERR_OK;
}

static err_t wsConnected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    if (err != ERR_OK) {
        printf("Connect failed %d\n", err);
        return ERR_ABRT;
    }
 
    printf("WebSocket Connected\r\n");
    state->upgraded = false;

    printf("Requesting WebSocket upgrade...\n");
    // Write HTTP GET Request with Websocket upgrade 
    state->buffer_len = sprintf((char*)state->en, 
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s\r\n",
        ip4addr_ntoa(&state->remote_addr), state->remote_port,
        state->additional_headers?state->additional_headers:"");
    //printf("[%.*s]\n",state->buffer_len,state->en);
    err = tcp_write(state->tcp_pcb, state->en, state->buffer_len, TCP_WRITE_FLAG_COPY);

    state->connected = TCP_CONNECTED;
    return ERR_OK;
}

static err_t wsPoll(void *arg, struct tcp_pcb *tpcb) {
    return ERR_OK;
}

static err_t wsClose(void *arg) {
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    err_t err = ERR_OK;
    if (state->tcp_pcb != NULL) {
        tcp_arg(state->tcp_pcb, NULL);
        tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        err = tcp_close(state->tcp_pcb);
        if (err != ERR_OK) {
            printf("Close failed %d, calling abort\n", err);
            tcp_abort(state->tcp_pcb);
            err = ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    state->connected = TCP_DISCONNECTED;
    state->upgraded = false;

    if ( state->auto_reconnect ) {
        // Reconnect
        wsConnect( (WebSocketClient_p)state );
    }

    return err;
}

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

err_t wsReceive(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) 
{
    WebSocketClient_t *state = (WebSocketClient_t*)arg;
    bool reply_pong = false;

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
                if ( state->upgraded  ) {
                    WebsocketPacketHeader_t header;
                    wsParsePacket(&header, (char *)q->payload, q->len);

                    switch( header.meta.bits.OPCODE ) {
                        case WEBSOCKET_OPCODE_PING:
                            // send pong
                            printf("Received PING\n");
                            reply_pong = true;
                            break;
                        case WEBSOCKET_OPCODE_CLOSE:
                            // close connection
                            int64_t error = -1;
                            if ( header.length == 2 ) {
                                error = ((uint8_t *)q->payload + header.start)[0]<<8 | ((uint8_t *)q->payload + header.start)[1];
                            }
                            printf("Connection close (%lld)\n",error);
                            break;
                        case WEBSOCKET_OPCODE_CONTINUE:
                        case WEBSOCKET_OPCODE_TEXT:
                        case WEBSOCKET_OPCODE_BIN:
                            printf("Received data (Op Code %d)\n",header.meta.bits.OPCODE);
                            memcpy(state->rx_buffer + state->rx_buffer_len, (uint8_t *)q->payload + header.start, header.length);
                            state->rx_buffer_len += header.length;
                            break;
                        default:
                            printf("Received unknown data (Op Code %d)\n",header.meta.bits.OPCODE);
                            break;
                    }
                } else {
                    memcpy(state->rx_buffer + state->rx_buffer_len, (uint8_t *)q->payload, q->len );
                    state->rx_buffer_len += q->len;
                }
            }
        }
        

        if ( state->upgraded && state->rx_buffer_len>0 ) {
            //printf("tcp_recved [%.*s]\n",state->rx_buffer_len,state->rx_buffer);
            if ( state->rx_buffer_len>=2 && state->rx_buffer[0]=='{' ) {
                if ( state->messageHandler ) {
                    if ( state->rx_buffer_len>=BUF_SIZE ) {
                        state->rx_buffer_len=BUF_SIZE-1;
                    }
                    state->rx_buffer[state->rx_buffer_len] = 0;
                    (state->messageHandler)( (WebSocketClient_p)state, (char *)state->rx_buffer, state->rx_buffer_len);
                }
            }
        } else {
            if ( strnstr((char *)state->rx_buffer, "HTTP/1.1 101", state->rx_buffer_len) != NULL &&
                    strnstr((char *)state->rx_buffer, "Connection: upgrade", state->rx_buffer_len) != NULL &&
                        strnstr((char *)state->rx_buffer, "Upgrade: websocket", state->rx_buffer_len) != NULL ) {
                //printf("tcp_recved [%.*s]\n",state->rx_buffer_len,state->rx_buffer);
                printf("WebSocket upgrade acknowladged\n");
                state->upgraded = true;
            }            
        }

        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);

    if ( reply_pong ) {
        // send pong
        state->buffer_len = wsBuildPacket((char *)state->en, BUF_SIZE, WEBSOCKET_OPCODE_PONG, NULL, 0, 1);
        if ( tcp_write(state->tcp_pcb, state->en, state->buffer_len, TCP_WRITE_FLAG_COPY) == ERR_OK) {
            printf("Sent PONG\n");
        } else {    
            printf("PONG send failed!\n");
        }
    }

    return ERR_OK;
}

static err_t wsTCPConnect(void *arg) 
{
    WebSocketClient_t *state = (WebSocketClient_t*)arg;

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
    state->connected = TCP_CONNECTING;
    state->upgraded = false;
    err_t err = tcp_connect(state->tcp_pcb, &state->remote_addr, state->remote_port, wsConnected);
    cyw43_arch_lwip_end();
        
    return err;
}

//===============================================================================================================

/*! \brief Initialises a WebSocket for connection
 *  \ingroup Websocket.c
 *
 * \param server ip address of target server
 * \param port port on which to connect
 * \param messageHandler callback to handle received messages
 * \param additioonalHeaders additional headers to include in the connect
 * \param autoReconnect automatically reconnect if the connection is closed
 * \return handle to a WebSocket client
 */
WebSocketClient_p wsCreate( const char *server, uint16_t port, wsMessagehandler messageHandler, char *additionalHeaders, bool autoReconnect )
{
    WebSocketClient_t *state = (WebSocketClient_t *)calloc(1, sizeof(WebSocketClient_t));
    if (!state) {
        printf("Failed to allocate WebSocket client\n");
        return NULL;
    }

    ip4addr_aton(server, &state->remote_addr);
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
    WebSocketClient_t *state = (WebSocketClient_t *)client;
    printf("WebSocket connecting to %s:%u\n", ip4addr_ntoa(&state->remote_addr), state->remote_port);
    return( wsTCPConnect(state)==ERR_OK );
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
    if ( state->additional_headers ) {
        free(state->additional_headers);
    }
    if ( client ) {
        free( client );
    }
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
    state->buffer_len = wsBuildPacket((char *)state->en, BUF_SIZE, WEBSOCKET_OPCODE_TEXT, text, len, 1);
    err_t err = tcp_write(state->tcp_pcb, state->en, state->buffer_len, TCP_WRITE_FLAG_COPY);
    return (err == ERR_OK);
}

/*! \brief Handles any WebSocket functionality, must be called periodically
 *  \ingroup Websocket.c
 *
 * \param Nothing
 * \return Nothing
 */
void wsHandler( void )
{

}

/*! \brief Gets local IP address
 *  \ingroup Websocket.c
 *
 * \param Nothing
 * \return pointer to local IP address 
 */
const char *wsGetLocalIPAddress( void ) 
{
    static char localIPAddress[16+10];
    ip_addr_t ip_address;

    // get ip address
    memcpy(&ip_address, &cyw43_state.netif[CYW43_ITF_STA].ip_addr, sizeof (ip_addr_t));
    strncpy( localIPAddress, ip4addr_ntoa(&ip_address), 16 );

    return localIPAddress;
}

/*! \brief Gets local MAC address
 *  \ingroup Websocket.c
 *
 * \param Nothing
 * \return pointer to local MAC address 
 */
const char *wsGetLocalMACAddress( void )
{
    static char localMACAddress[18+10];

        // get the mac address
    for ( int i=0 ; i < 6 ; i++ ) {
        sprintf(&localMACAddress[i*3], i<5?"%02X-":"%02X",cyw43_state.mac[i]);
    }

    return localMACAddress;
}