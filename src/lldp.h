/*
 * Minimal LLDP (IEEE 802.1AB) for drone.
 *
 * TX: every cfg->lldp_interval seconds, send a frame on `netif` advertising
 * our Chassis-ID/Port-ID/TTL/System-Name/System-Description/Management-Address
 * so each LLDP-speaking neighbor (e.g. the switch lldpd) shows us in its
 * neighbor table.
 *
 * RX: register a raw_ethertype handler for 0x88CC, parse incoming TLVs, and
 * remember the latest Management-Address TLV we see.  mqtt_app reads it via
 * lldp_get_neighbor_addr() to learn where the broker lives.
 *
 * No neighbor cache: the most-recent frame's mgmt-addr wins.  TTL expiry is
 * not enforced in v1; if frames stop arriving we keep the last-known address.
 */
#ifndef LLDP_H
#define LLDP_H

#include <stdbool.h>

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

#include "net.h"

/* Register the LLDP RX handler and start the periodic TX task on `netif`.
 * Returns true on success; on failure (mutex/registry/task) returns false so
 * the caller can fail loudly instead of waiting forever for frames that will
 * never be parsed. */
bool lldp_init(struct netif *netif, const struct app_cfg *cfg);

/* Copy the latest neighbor IPv4 mgmt address into *out; returns true if a
 * frame carrying a Management-Address TLV (IPv4 subtype) has been seen. */
bool lldp_get_neighbor_addr(ip_addr_t *out);

#endif /* LLDP_H */
