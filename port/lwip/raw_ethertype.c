/*
 * See raw_ethertype.h.  Tiny static-array registry; four slots is plenty for
 * the protocols a drone is likely to grow (LLDP today, perhaps LACP/EAPOL
 * later).  Lookup is O(N), but N is at most 4.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#include "raw_ethertype.h"

#define MAX_HANDLERS	4
#define ETH_TYPE_OFFSET	12

static struct {
	raw_ethertype_fn fn;	    /* written first */
	uint16_t	 ethertype; /* 0 = empty slot; written last (publish) */
} s_handlers[MAX_HANDLERS];

bool raw_ethertype_register(uint16_t ethertype, raw_ethertype_fn handler)
{
	int free_slot = -1;

	if ((ethertype == 0) || (handler == NULL)) {
		return false;
	}

	/* With the lldp_register/lldp_start phase split the first caller
	 * now registers BEFORE qn_rx_task is created, so there's no
	 * concurrent dispatch the first time; the critical section still
	 * guards against a (currently hypothetical) second registrant
	 * coming in after the netif is up.  Writing fn before ethertype
	 * means a reader that observes ethertype != 0 also sees a
	 * fully-initialized fn pointer on weakly-ordered targets. */
	taskENTER_CRITICAL();
	for (int i = 0; i < MAX_HANDLERS; i++) {
		if (s_handlers[i].ethertype == ethertype) {
			taskEXIT_CRITICAL();
			return false; /* already taken */
		}
		if ((s_handlers[i].ethertype == 0) && (free_slot < 0)) {
			free_slot = i;
		}
	}

	if (free_slot < 0) {
		taskEXIT_CRITICAL();
		return false; /* registry full */
	}

	s_handlers[free_slot].fn = handler;
	s_handlers[free_slot].ethertype = ethertype;
	taskEXIT_CRITICAL();
	return true;
}

bool raw_ethertype_dispatch(struct pbuf *p, struct netif *netif)
{
	uint8_t et_bytes[2];
	uint16_t et;

	/* Use tot_len + pbuf_copy_partial so chained pbufs work (the qeneth
	 * netif always hands us a single-segment pbuf today, but a future
	 * netif might not).  EtherType lives at offset 12 in the Ethernet
	 * header. */
	if (p->tot_len < sizeof(struct eth_hdr)) {
		return false;
	}
	if (pbuf_copy_partial(p, et_bytes, 2, ETH_TYPE_OFFSET) != 2) {
		return false;
	}
	et = ((uint16_t)et_bytes[0] << 8) | et_bytes[1];

	for (int i = 0; i < MAX_HANDLERS; i++) {
		if (s_handlers[i].ethertype == et) {
			s_handlers[i].fn(p, netif);
			return true;
		}
	}

	return false;
}
