/*
 * Raw EtherType dispatch for non-IP protocols (LLDP today, room for LACP,
 * EAPOL, ...).  Netifs call raw_ethertype_dispatch() before handing a frame
 * to tcpip_input; if a handler is registered for the frame's EtherType, the
 * handler takes ownership of the pbuf (must pbuf_free it) and the netif
 * skips the lwIP IP stack for that frame.
 */
#ifndef PORT_LWIP_RAW_ETHERTYPE_H
#define PORT_LWIP_RAW_ETHERTYPE_H

#include <stdbool.h>
#include <stdint.h>

#include "lwip/pbuf.h"
#include "lwip/netif.h"

typedef void (*raw_ethertype_fn)(struct pbuf *p, struct netif *netif);

/* Register a handler for one EtherType (host byte order).  Returns false if
 * the registry is full or the EtherType is already taken. */
bool raw_ethertype_register(uint16_t ethertype, raw_ethertype_fn handler);

/* Look up a handler for the frame's EtherType.  On match, call it and return
 * true (the handler now owns the pbuf and must pbuf_free it).  On miss,
 * return false and leave the pbuf untouched so the caller passes it on. */
bool raw_ethertype_dispatch(struct pbuf *p, struct netif *netif);

#endif /* PORT_LWIP_RAW_ETHERTYPE_H */
