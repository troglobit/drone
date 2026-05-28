/*
 * See lldp.h for the design summary.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"

#include "raw_ethertype.h"
#include "lldp.h"

#define LLDP_ETHERTYPE		0x88CC

/* IEEE 802.1AB "nearest bridge" group address.  Bridges drop frames addressed
 * to this MAC, which scopes LLDP to one wire. */
static const uint8_t LLDP_DST_MAC[6] = { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E };

/* TLV type values */
#define TLV_END			0
#define TLV_CHASSIS_ID		1
#define TLV_PORT_ID		2
#define TLV_TTL			3
#define TLV_SYS_NAME		5
#define TLV_SYS_DESC		6
#define TLV_MGMT_ADDR		8

/* Chassis-ID subtypes */
#define CHASSIS_ID_MAC		4

/* Port-ID subtypes */
#define PORT_ID_MAC		3

/* Management-Address address subtypes (IANA address family numbers) */
#define MGMT_ADDR_IPV4		1

/* Management-Address interface subtypes */
#define MGMT_IF_IFINDEX		2

#define LLDP_TX_TASK_PRIO	(tskIDLE_PRIORITY + 1)
#define LLDP_TX_TASK_STACK	(configMINIMAL_STACK_SIZE * 4)

/* Sized to one Ethernet MTU.  Real lldpd LLDPDUs from a switch routinely
 * reach several hundred bytes (Sys-Desc, Port-Desc, 802.1/802.3 OUI TLVs,
 * Capabilities); the Management-Address TLV may sit anywhere among them,
 * so a 256-byte cap would silently truncate it on real hardware. */
#define LLDP_FRAME_MAX		1536
#define LLDP_SYS_DESC		"drone (FreeRTOS+lwIP)"

static uint16_t		 s_interval; /* seconds between TX */
static uint16_t		 s_ttl;	     /* TTL value we advertise */
static char		 s_sysname[32];

static SemaphoreHandle_t s_addr_mutex;
static ip_addr_t	 s_neighbor_addr;
static bool		 s_have_addr;

/*------------------------------------------------------------------*/
/* TLV encode helpers                                                */

static int put_u16(uint8_t *buf, int off, uint16_t v)
{
	buf[off++] = (uint8_t)(v >> 8);
	buf[off++] = (uint8_t)(v & 0xFF);
	return off;
}

static int put_u32(uint8_t *buf, int off, uint32_t v)
{
	buf[off++] = (uint8_t)(v >> 24);
	buf[off++] = (uint8_t)(v >> 16);
	buf[off++] = (uint8_t)(v >> 8);
	buf[off++] = (uint8_t)(v & 0xFF);
	return off;
}

static int put_tlv(uint8_t *buf, int off, int bufsize, uint8_t type,
		   uint16_t length, const void *value)
{
	uint16_t hdr;

	if (off + 2 + length > bufsize) {
		return -1;
	}

	hdr = ((uint16_t)(type & 0x7F) << 9) | (length & 0x1FF);
	off = put_u16(buf, off, hdr);
	if ((length > 0) && (value != NULL)) {
		memcpy(buf + off, value, length);
		off += length;
	}
	return off;
}

/*------------------------------------------------------------------*/
/* TX path                                                           */

/* Build an LLDPDU into buf.  Returns the frame length, or -1 on overflow.
 * Caller holds nothing in particular; netif fields read here (hwaddr, the
 * v4 addr, the interface index) are stable enough that we don't need the
 * core lock just to read them. */
static int build_frame(uint8_t *buf, int bufsize, struct netif *netif)
{
	uint8_t tmp[32];
	int off = 0;
	int n;

	if (bufsize < 64) {
		return -1;
	}

	/* Ethernet header: dst, src, ethertype. */
	memcpy(buf + off, LLDP_DST_MAC, 6);
	off += 6;
	memcpy(buf + off, netif->hwaddr, 6);
	off += 6;
	off = put_u16(buf, off, LLDP_ETHERTYPE);

	/* Chassis-ID TLV (mandatory, first).  Subtype = MAC. */
	tmp[0] = CHASSIS_ID_MAC;
	memcpy(tmp + 1, netif->hwaddr, 6);
	off = put_tlv(buf, off, bufsize, TLV_CHASSIS_ID, 7, tmp);
	if (off < 0) {
		return -1;
	}

	/* Port-ID TLV (mandatory, second).  Subtype = MAC. */
	tmp[0] = PORT_ID_MAC;
	memcpy(tmp + 1, netif->hwaddr, 6);
	off = put_tlv(buf, off, bufsize, TLV_PORT_ID, 7, tmp);
	if (off < 0) {
		return -1;
	}

	/* TTL TLV (mandatory, third). */
	n = put_u16(tmp, 0, s_ttl);
	off = put_tlv(buf, off, bufsize, TLV_TTL, (uint16_t)n, tmp);
	if (off < 0) {
		return -1;
	}

	/* System-Name TLV (optional). */
	n = (int)strlen(s_sysname);
	if (n > 0) {
		off = put_tlv(buf, off, bufsize, TLV_SYS_NAME, (uint16_t)n,
			      s_sysname);
		if (off < 0) {
			return -1;
		}
	}

	/* System-Description TLV (optional). */
	off = put_tlv(buf, off, bufsize, TLV_SYS_DESC,
		      (uint16_t)(sizeof(LLDP_SYS_DESC) - 1), LLDP_SYS_DESC);
	if (off < 0) {
		return -1;
	}

	/* Management-Address TLV (optional) - only if we have a v4 address. */
	{
		const ip4_addr_t *v4 = netif_ip4_addr(netif);

		if (!ip4_addr_isany(v4)) {
			int t = 0;

			tmp[t++] = 5; /* addr-string-length: 1 subtype + 4 addr */
			tmp[t++] = MGMT_ADDR_IPV4;
			memcpy(tmp + t, &v4->addr, 4); /* already net-order */
			t += 4;
			tmp[t++] = MGMT_IF_IFINDEX;
			t = put_u32(tmp, t, netif_get_index(netif));
			tmp[t++] = 0; /* OID length */
			off = put_tlv(buf, off, bufsize, TLV_MGMT_ADDR,
				      (uint16_t)t, tmp);
			if (off < 0) {
				return -1;
			}
		}
	}

	/* End-of-LLDPDU TLV (mandatory, last). */
	off = put_tlv(buf, off, bufsize, TLV_END, 0, NULL);
	if (off < 0) {
		return -1;
	}

	return off;
}

static void lldp_tx_task(void *arg)
{
	struct netif *netif = arg;
	uint8_t buf[LLDP_FRAME_MAX];

	for (;;) {
		int len = build_frame(buf, sizeof(buf), netif);

		if (len > 0) {
			struct pbuf *p =
				pbuf_alloc(PBUF_RAW_TX, (u16_t)len, PBUF_RAM);

			if (p != NULL) {
				pbuf_take(p, buf, (u16_t)len);
				/* qn_linkoutput touches no lwIP state, so we
				 * deliberately call it without the core lock:
				 * holding it across a blocking sendto() would
				 * stall the entire tcpip thread (mqtt, mdns,
				 * ping) while the host UDP send buffer
				 * drains. */
				netif->linkoutput(netif, p);
				pbuf_free(p);
			}
		}

		vTaskDelay(pdMS_TO_TICKS((uint32_t)s_interval * 1000));
	}
}

/*------------------------------------------------------------------*/
/* RX path                                                           */

/* Walk an LLDPDU looking for the first Management-Address TLV with an IPv4
 * subtype; update s_neighbor_addr.  Other TLVs are skipped. */
static void parse_lldpdu(const uint8_t *buf, int len)
{
	int off = 14; /* skip Ethernet header */

	while (off + 2 <= len) {
		uint16_t hdr = ((uint16_t)buf[off] << 8) | buf[off + 1];
		uint8_t	 type = (uint8_t)((hdr >> 9) & 0x7F);
		uint16_t tlen = (uint16_t)(hdr & 0x1FF);

		off += 2;
		if (off + tlen > len) {
			return;
		}

		if (type == TLV_END) {
			return;
		}

		if ((type == TLV_MGMT_ADDR) && (tlen >= 1)) {
			uint8_t addr_str_len = buf[off];

			/* IEEE 802.1AB: addr-string-length is 1 (subtype) +
			 * address length; for IPv4 that's exactly 5. */
			if ((addr_str_len == 5) &&
			    ((uint16_t)(1 + addr_str_len) <= tlen) &&
			    (buf[off + 1] == MGMT_ADDR_IPV4)) {
				ip_addr_t addr;
				bool	  changed;
				char	  log[24];

				ip_addr_set_zero(&addr);
				IP_SET_TYPE(&addr, IPADDR_TYPE_V4);
				memcpy(&ip_2_ip4(&addr)->addr, buf + off + 2,
				       4);

				xSemaphoreTake(s_addr_mutex, portMAX_DELAY);
				changed = !s_have_addr ||
					  !ip_addr_eq(&addr, &s_neighbor_addr);
				if (changed) {
					s_neighbor_addr = addr;
					s_have_addr = true;
				}
				xSemaphoreGive(s_addr_mutex);

				/* Log outside the critical section: printf may
				 * block on a piped stdout and we don't want to
				 * hold s_addr_mutex while it does. */
				if (changed) {
					snprintf(log, sizeof(log), "%s",
						 ip4addr_ntoa(ip_2_ip4(&addr)));
					printf("lldp: neighbor mgmt addr %s\n",
					       log);
				}
				return;
			}
		}

		off += tlen;
	}
}

static void lldp_rx_handler(struct pbuf *p, struct netif *netif)
{
	uint8_t buf[LLDP_FRAME_MAX];
	u16_t	n;

	LWIP_UNUSED_ARG(netif);

	n = pbuf_copy_partial(p, buf, sizeof(buf), 0);
	if (n >= 14) {
		parse_lldpdu(buf, (int)n);
	}
	pbuf_free(p);
}

bool lldp_get_neighbor_addr(ip_addr_t *out)
{
	bool have;

	/* Defense in depth: lldp_init's failure is propagated to main, so we
	 * shouldn't reach here with a NULL mutex.  If we do, return "no addr
	 * known" rather than crashing on xSemaphoreTake(NULL, ...). */
	if (s_addr_mutex == NULL) {
		return false;
	}

	xSemaphoreTake(s_addr_mutex, portMAX_DELAY);
	have = s_have_addr;
	if (have && (out != NULL)) {
		*out = s_neighbor_addr;
	}
	xSemaphoreGive(s_addr_mutex);
	return have;
}

/*------------------------------------------------------------------*/

bool lldp_init(struct netif *netif, const struct app_cfg *cfg)
{
	s_interval = (uint16_t)cfg->lldp_interval;
	s_ttl = (uint16_t)cfg->lldp_ttl;
	snprintf(s_sysname, sizeof(s_sysname), "%s", cfg->hostname);

	s_addr_mutex = xSemaphoreCreateMutex();
	if (s_addr_mutex == NULL) {
		fprintf(stderr, "lldp: failed to create mutex\n");
		return false;
	}

	if (!raw_ethertype_register(LLDP_ETHERTYPE, lldp_rx_handler)) {
		fprintf(stderr, "lldp: failed to register RX handler\n");
		return false;
	}

	if (xTaskCreate(lldp_tx_task, "lldp", LLDP_TX_TASK_STACK, netif,
			LLDP_TX_TASK_PRIO, NULL) != pdPASS) {
		fprintf(stderr, "lldp: failed to create TX task\n");
		return false;
	}

	return true;
}
