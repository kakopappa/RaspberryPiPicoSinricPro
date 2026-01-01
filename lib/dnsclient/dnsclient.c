/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "dnsclient.h"

typedef struct _dns_state {
    ip_addr_t dns_address;
    int status;
} dns_state_t;

// Call back with a DNS result
static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) 
{
    dns_state_t *state = (dns_state_t *)arg;
    if (ipaddr) {
        state->dns_address = *ipaddr;
        state->status = ERR_OK;
    } else {
        printf("DNS request failed\n");
        state->status = -1;
    }
}

// Get DNS address...
int get_dns_address(const char *hostname, ip_addr_t *ip_address) 
{
    static dns_state_t state;
    int err = ERR_OK;

    state.dns_address.addr = 0;
    state.status = 1;

    while( state.status>0 ) {

        if ( state.status == 1 ) {
            // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
            // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
            // these calls are a no-op and can be omitted, but it is a good practice to use them in
            // case you switch the cyw43_arch type later.
            cyw43_arch_lwip_begin();
            printf("DNS lookup for '%s'\n", hostname);
            err = dns_gethostbyname(hostname, &state.dns_address, dns_found, (void *)&state);
            cyw43_arch_lwip_end();
        }

        if (err == ERR_OK) {
            state.status = ERR_OK;
        } else if (err == ERR_INPROGRESS) { // ERR_INPROGRESS means expect a callback
            state.status = 2;
#if PICO_CYW43_ARCH_POLL
            // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
            // main loop (not from a timer interrupt) to check for Wi-Fi driver or lwIP work that needs to be done.
            cyw43_arch_poll();
            // you can poll as often as you like, however if you have nothing else to do you can
            // choose to sleep until either a specified time, or cyw43_arch_poll() has work to do:
            cyw43_arch_wait_for_work_until(state->dns_request_sent ? at_the_end_of_time : state->ntp_test_time);
#else
            // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
            // is done via interrupt in the background. This sleep is just an example of some (blocking)
            // work you might be doing.
            sleep_ms(100);
#endif
        } else {
            printf("DNS request failed\n");
            state.status = -1;
        } 
    }

    if ( state.status == ERR_OK ) {
        printf("DNS address is %s\n", ipaddr_ntoa(&state.dns_address));
        *ip_address = state.dns_address;
    }

    return state.status;
}

