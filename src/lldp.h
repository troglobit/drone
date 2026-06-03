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

/* Bring-up is split into two phases so the RX handler is registered
 * BEFORE the netif's RX task can dispatch any frame -- otherwise a peer
 * already pumping LLDP into our UDP socket can land its first frame
 * before raw_ethertype_register() runs and have it dropped at
 * ethernet_input as unknown EtherType.
 *
 *   lldp_register(cfg)   -- mutex + RX handler.  Call BEFORE net_start.
 *   lldp_start(netif)    -- spawn the TX task.   Call AFTER  net_start.
 *
 * Both return true on success; on failure they print a diagnostic and
 * the caller should bail (drone has no recovery path for partial init). */
bool lldp_register(const struct app_cfg *cfg);
bool lldp_start(struct netif *netif);

/* Copy the latest neighbor IPv4 mgmt address into *out; returns true if a
 * frame carrying a Management-Address TLV (IPv4 subtype) has been seen. */
bool lldp_get_neighbor_addr(ip_addr_t *out);

/* One-shot callback fired from the LLDP RX path the first time any
 * neighbor frame arrives.  Used to gate DHCP startup so drone doesn't
 * burn the early DISCOVER retries (with their fast exponential backoff)
 * before the upstream switch/Linux peer is online -- the LLDP frame
 * itself is a decent proxy for "upstream network stack is alive".
 * Register before qeneth_netif_add() spawns the RX task (the second
 * net.c bring-up step) or any time before the first frame; the
 * callback runs in the netif RX-task context (no lwIP core lock held). */
typedef void (*lldp_neighbor_seen_cb)(void);
void lldp_on_neighbor_seen(lldp_neighbor_seen_cb cb);

#endif /* LLDP_H */
