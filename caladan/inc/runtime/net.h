/*
 * net.h - shared network definitions
 */

#pragma once

#include <netdb.h>

#include <base/types.h>

#define DEFAULT_DSCP IPTOS_DSCP_CS4

struct netaddr {
	uint32_t ip;
	uint16_t port;
};

extern uint32_t get_cfg_ip(void);

extern int str_to_netaddr(const char *str, struct netaddr *addr);

extern uint32_t addrinfo_to_ip(const struct addrinfo *res);
