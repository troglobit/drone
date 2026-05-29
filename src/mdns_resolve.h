/*
 * One-shot mDNS A-record querier for ".local" hostnames.
 *
 * lwIP's apps/mdns ships only the responder side; this querier lets drone
 * resolve --broker broker1.local without pulling in a unicast-DNS stack
 * (LWIP_DNS stays 0).  The query is multicast to 224.0.0.251:5353 with the
 * QU (unicast-response-requested) bit set so the answer comes back unicast
 * to our ephemeral source port -- the local responder owns multicast 5353
 * and we don't want to fight it.
 *
 * Synchronous + blocking + thread-safe (concurrent callers serialize on an
 * internal mutex).  MUST NOT be called from the tcpip thread: it waits on
 * a semaphore that the UDP recv callback (which runs on tcpip) gives.
 */
#ifndef MDNS_RESOLVE_H
#define MDNS_RESOLVE_H

#include <stdbool.h>
#include <stdint.h>

#include "lwip/ip_addr.h"

/* Allocate the querier's UDP PCB and the internal mutexes.  Call once after
 * net_start() (a netif must exist for udp_sendto to pick one for the
 * multicast).  Returns true on success. */
bool mdns_resolve_init(void);

/* Send one mDNS A-record query for `name` (e.g. "broker1.local") and wait up
 * to `timeout_ms` for the response.  Returns true and fills *out on
 * success; false on timeout, malformed answer, or any error.  Each call
 * sends exactly one query packet -- callers that want retry behaviour put
 * this in a loop with their own cadence. */
bool mdns_resolve(const char *name, ip_addr_t *out, uint32_t timeout_ms);

#endif /* MDNS_RESOLVE_H */
