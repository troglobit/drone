/*
 * Minimal raw-API ICMP echo originator - a built-in diagnostic.  The device
 * answers echo requests automatically (LWIP_ICMP); this lets it send them too.
 */
#ifndef PING_H
#define PING_H

#include "lwip/ip_addr.h"

/* Spawn a task that sends 'count' echo requests (1/s) to 'target'. */
void ping_start(const ip4_addr_t *target, int count);

#endif /* PING_H */
