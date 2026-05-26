/*
 * qeneth_netif - an lwIP Ethernet network interface that rides on a host UDP
 * socket using the same framing as QEMU's "-netdev socket,udp=..." links
 * (one raw Ethernet frame per UDP datagram, no length prefix).  This is how
 * frtos-dev attaches to a qeneth topology without being a QEMU instance.
 */
#ifndef QENETH_NETIF_H
#define QENETH_NETIF_H

#include <stdint.h>
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"

/*
 * Bring up the interface.  'sockfd' must be an already-bound UDP socket;
 * datagrams are sent to (peer_ip_be, peer_port_be), both in network byte
 * order.  netif_add()/set_up() are performed under the tcpip core lock and a
 * receive task is started.  Returns ERR_OK on success.
 */
err_t qeneth_netif_add( struct netif * netif,
                        const ip4_addr_t * ip,
                        const ip4_addr_t * netmask,
                        const ip4_addr_t * gw,
                        const uint8_t mac[ 6 ],
                        int sockfd,
                        uint32_t peer_ip_be,
                        uint16_t peer_port_be );

#endif /* QENETH_NETIF_H */
