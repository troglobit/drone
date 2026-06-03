/*
 * See qeneth_netif.h for the design summary.
 */
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#if LWIP_IPV6
#include "lwip/ethip6.h"
#endif
#include "lwip/tcpip.h"
#include "lwip/stats.h"
#include "netif/ethernet.h"

#include "qeneth_netif.h"
#include "raw_ethertype.h"

#define QN_FRAME_MAX 1536
#define QN_RX_TASK_PRIO (configMAX_PRIORITIES - 3)
#define QN_RX_TASK_STACK (configMINIMAL_STACK_SIZE * 4)

struct qn_state {
	int fd;			 /* host UDP socket (bound to local addr) */
	struct sockaddr_in peer; /* datagram destination */
	uint8_t mac[6];
	const char *hostname;	 /* caller-owned; lwIP DHCP option 12 */
	struct netif *netif;
};

/* Single interface (LWIP_SINGLE_NETIF), so one static state is enough. */
static struct qn_state qn;

/* lwIP calls this (from the tcpip thread) to put a frame on the wire. */
static err_t qn_linkoutput(struct netif *netif, struct pbuf *p)
{
	struct qn_state *s = netif->state;
	uint8_t buf[QN_FRAME_MAX];
	ssize_t sent;

	if (p->tot_len > sizeof(buf)) {
		LINK_STATS_INC(link.lenerr);
		return ERR_IF;
	}

	/* Flatten the (possibly chained) pbuf into one datagram payload. */
	pbuf_copy_partial(p, buf, p->tot_len, 0);

	sent = sendto(s->fd, buf, p->tot_len, 0, (struct sockaddr *)&s->peer,
		      sizeof(s->peer));
	if (sent != (ssize_t)p->tot_len) {
		LINK_STATS_INC(link.err);
		return ERR_IF;
	}

	LINK_STATS_INC(link.xmit);
	return ERR_OK;
}

/*
 * Receive loop.  Each datagram is one Ethernet frame.
 *
 * The POSIX simulator schedules FreeRTOS tasks cooperatively (one pthread runs
 * at a time); a task parked in a *blocking* host syscall still looks "ready"
 * to the scheduler and would starve lower-priority tasks and the idle task.
 * So we drain the socket non-blocking and vTaskDelay() between sweeps, which
 * yields properly.  On the real MCU this interface becomes IRQ/DMA-driven.
 */
static void qn_rx_task(void *arg)
{
	struct qn_state *s = arg;
	uint8_t buf[QN_FRAME_MAX];

	for (;;) {
		ssize_t n;

		while ((n = recvfrom(s->fd, buf, sizeof(buf), MSG_DONTWAIT,
				     NULL, NULL)) > 0) {
			struct pbuf *p =
				pbuf_alloc(PBUF_RAW, (u16_t)n, PBUF_POOL);

			if (p == NULL) {
				LINK_STATS_INC(link.drop);
				continue;
			}

			pbuf_take(p, buf, (u16_t)n);
			LINK_STATS_INC(link.recv);

			/* Raw EtherType handlers (LLDP, ...) consume frames
			 * before they reach the IP stack.  netif->input is
			 * tcpip_input(): thread-safe hand-off to tcpip. */
			if (raw_ethertype_dispatch(p, s->netif)) {
				/* handler now owns p */
			} else if (s->netif->input(p, s->netif) != ERR_OK) {
				pbuf_free(p);
			}
		}

		vTaskDelay(pdMS_TO_TICKS(1));
	}
}

/* netif_add() init callback - runs with the core lock held. */
static err_t qn_netif_init(struct netif *netif)
{
	struct qn_state *s = netif->state;

	netif->name[0] = 'q';
	netif->name[1] = '0';
	netif->output = etharp_output;
#if LWIP_IPV6
	netif->output_ip6 = ethip6_output;
#endif
	netif->linkoutput = qn_linkoutput;
	netif->mtu = 1500;
	netif->hwaddr_len = ETH_HWADDR_LEN;
	memcpy(netif->hwaddr, s->mac, ETH_HWADDR_LEN);
	/* NETIF_FLAG_LINK_UP must NOT be pre-set here: lwIP's
	 * netif_set_link_up() is guarded by !(flags & LINK_UP) and would
	 * otherwise short-circuit, skipping autoip_network_changed_link_up(),
	 * the link callback, and the LWIP_NSC_LINK_CHANGED ext event.  Let
	 * qeneth_netif_add() raise it. */
	netif->flags =
		NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
#if LWIP_IGMP
	/* Required for v4 multicast joins (mDNS joins 224.0.0.251). */
	netif->flags |= NETIF_FLAG_IGMP;
#endif
#if LWIP_IPV6 && LWIP_IPV6_MLD
	netif->flags |= NETIF_FLAG_MLD6;
#endif
#if LWIP_NETIF_HOSTNAME
	/* Set inside the netif_add init path so the RX task spawned at the
	 * end of qeneth_netif_add() can't see netif->hostname==NULL: lwIP's
	 * DHCP client emits option 12 from this pointer, and a NULL there
	 * defeats dnsmasq's hostname-keyed static-lease matching. */
	netif->hostname = s->hostname;
#endif
	return ERR_OK;
}

err_t qeneth_netif_add(struct netif *netif, const ip4_addr_t *ip,
		       const ip4_addr_t *netmask, const ip4_addr_t *gw,
		       const uint8_t mac[6], const char *hostname, int sockfd,
		       uint32_t peer_ip_be, uint16_t peer_port_be)
{
	qn.fd = sockfd;
	memset(&qn.peer, 0, sizeof(qn.peer));
	qn.peer.sin_family = AF_INET;
	qn.peer.sin_addr.s_addr = peer_ip_be;
	qn.peer.sin_port = peer_port_be;
	memcpy(qn.mac, mac, sizeof(qn.mac));
	qn.hostname = hostname;
	qn.netif = netif;

	LOCK_TCPIP_CORE();
	if (netif_add(netif, ip, netmask, gw, &qn, qn_netif_init,
		      tcpip_input) == NULL) {
		UNLOCK_TCPIP_CORE();
		return ERR_IF;
	}
	netif_set_default(netif);
#if LWIP_IPV6
	/* Derive fe80::EUI64 from the MAC; DAD then runs when the netif comes
	 * up. */
	netif_create_ip6_linklocal_address(netif, 1);
	netif_set_ip6_autoconfig_enabled(netif, 1);
#endif
	netif_set_link_up(netif);
	netif_set_up(netif);
	UNLOCK_TCPIP_CORE();

	if (xTaskCreate(qn_rx_task, "net-rx", QN_RX_TASK_STACK, &qn,
			QN_RX_TASK_PRIO, NULL) != pdPASS) {
		return ERR_MEM;
	}

	return ERR_OK;
}
