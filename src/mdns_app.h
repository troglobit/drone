/*
 * mDNS responder: announce <hostname>.local on the netif and register a
 * _drone._tcp service with TXT records describing the device.  This lets a
 * CNC discover drones on the lab L2 (`avahi-browse _drone._tcp`) and resolve
 * the host name without out-of-band IP configuration.
 */
#ifndef MDNS_APP_H
#define MDNS_APP_H

#include "net.h"

/* Start the responder (call after net_start()). */
void mdns_app_start( const struct app_cfg * cfg );

#endif /* MDNS_APP_H */
