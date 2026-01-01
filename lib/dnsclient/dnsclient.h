#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "pico/cyw43_arch.h"

int get_dns_address(const char *hostname, ip_addr_t *ip_address);

#ifdef __cplusplus
}
#endif
