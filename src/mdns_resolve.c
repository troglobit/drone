/*
 * See mdns_resolve.h for the design summary.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/opt.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"

#include "mdns_resolve.h"

#define MDNS_PORT		5353
#define MDNS_TYPE_A		1
#define MDNS_CLASS_IN		1
#define MDNS_QU_BIT		0x8000 /* QCLASS bit 15: unicast-response */
#define MDNS_CACHE_FLUSH_MASK	0x7FFF /* CLASS bit 15 in answers: mask it out */

#define MDNS_FRAME_MAX		512    /* sized for a typical A-record answer */
#define MAX_LABELS		16     /* compression-loop guard */

static SemaphoreHandle_t s_lock;     /* serialize concurrent callers */
static SemaphoreHandle_t s_resp_sem; /* signalled when a response arrives */
static struct udp_pcb	*s_pcb;
static bool		 s_inited;

/* Per-call slot, valid while s_lock is held. */
static ip_addr_t	 s_resp_addr;
static uint16_t		 s_resp_id;	/* DNS ID of the last response seen */
static bool		 s_resp_have;
static uint16_t		 s_next_id = 1; /* 0 is also valid but RFC 6762 §18.1
					 * allows non-zero for one-shot
					 * queriers; non-zero lets us distinguish
					 * a late callback from a fresh answer */

/*------------------------------------------------------------------*/
/* DNS name helpers                                                  */

/* Encode "BROKER1.Local" as "\x07broker1\x05local\x00".  Returns the encoded
 * length, or -1 on overflow / malformed input.  RFC 6762 §16 mandates
 * case-insensitive comparison, but lwIP's responder uses case-sensitive
 * memcmp; lowercasing on the wire ensures both ends agree. */
static int encode_name(uint8_t *buf, int bufsize, const char *name)
{
	int off = 0;
	const char *p = name;

	/* Tolerate FQDN trailing dot ("broker1.local."). */
	size_t name_len = strlen(name);
	if ((name_len > 0) && (name[name_len - 1] == '.')) {
		name_len--;
	}
	const char *end = name + name_len;

	while (p < end) {
		const char *dot = memchr(p, '.', (size_t)(end - p));
		int len = dot ? (int)(dot - p) : (int)(end - p);

		if ((len == 0) || (len > 63)) {
			return -1;
		}
		if (off + 1 + len > bufsize) {
			return -1;
		}
		buf[off++] = (uint8_t)len;
		for (int i = 0; i < len; i++) {
			buf[off++] = (uint8_t)tolower((unsigned char)p[i]);
		}
		p += len;
		if (p < end && *p == '.') {
			p++;
		}
	}
	if (off + 1 > bufsize) {
		return -1;
	}
	buf[off++] = 0; /* root label */
	return off;
}

/* Skip past a DNS name in buf starting at offset off; handles compression
 * pointers (which we don't follow -- we only need to advance past the name
 * to reach the TYPE/CLASS/TTL/RDLENGTH/RDATA that follows).  Returns the
 * offset of the byte after the name, or -1 on error. */
static int skip_name(const uint8_t *buf, int off, int len)
{
	int labels = 0;

	while (labels++ < MAX_LABELS) {
		if (off >= len) {
			return -1;
		}
		uint8_t b = buf[off];
		if (b == 0) {
			return off + 1; /* end-of-name */
		}
		if ((b & 0xC0) == 0xC0) {
			/* Compression pointer: 2 bytes total; the name
			 * continues elsewhere but we don't follow it. */
			if (off + 1 >= len) {
				return -1;
			}
			return off + 2;
		}
		if ((b & 0xC0) != 0) {
			return -1; /* reserved label type */
		}
		off += 1 + b;
	}
	return -1; /* compression loop or runaway name */
}

/*------------------------------------------------------------------*/
/* UDP recv callback -- runs on the tcpip thread                     */

static void mdns_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
			 const ip_addr_t *addr, u16_t port)
{
	uint8_t buf[MDNS_FRAME_MAX];
	u16_t	n;
	uint16_t id;
	uint16_t flags;
	uint16_t qdcount;
	uint16_t ancount;
	int	 off;

	LWIP_UNUSED_ARG(arg);
	LWIP_UNUSED_ARG(pcb);
	LWIP_UNUSED_ARG(addr);

	/* Only accept frames sourced from the mDNS port; a stray UDP packet
	 * landing on our ephemeral port (or a spoofed answer) is ignored. */
	if (port != MDNS_PORT) {
		pbuf_free(p);
		return;
	}

	n = pbuf_copy_partial(p, buf, sizeof(buf), 0);
	pbuf_free(p);

	if (n < 12) {
		return;
	}

	id = ((uint16_t)buf[0] << 8) | buf[1];
	flags = ((uint16_t)buf[2] << 8) | buf[3];
	qdcount = ((uint16_t)buf[4] << 8) | buf[5];
	ancount = ((uint16_t)buf[6] << 8) | buf[7];

	if ((flags & 0x8000) == 0) {
		return; /* not a response (QR bit clear) */
	}
	if (ancount == 0) {
		return;
	}
	/* Cap loop bounds: a hostile or malformed answer can claim arbitrary
	 * counts, and we walk those loops on the tcpip thread. */
	if ((qdcount > 8) || (ancount > 16)) {
		return;
	}

	off = 12;

	/* Echoed question section. */
	for (int i = 0; i < qdcount; i++) {
		off = skip_name(buf, off, n);
		if ((off < 0) || (off + 4 > n)) {
			return;
		}
		off += 4; /* QTYPE + QCLASS */
	}

	/* Walk answers; take the first IPv4 A record we see.  Transaction-
	 * ID matching is enforced by the caller (mdns_resolve) after the
	 * semaphore take, so we just stash whichever ID came in. */
	for (int i = 0; i < ancount; i++) {
		uint16_t type;
		uint16_t cls;
		uint16_t rdlen;

		off = skip_name(buf, off, n);
		if ((off < 0) || (off + 10 > n)) {
			return;
		}
		type = ((uint16_t)buf[off] << 8) | buf[off + 1];
		cls = ((uint16_t)buf[off + 2] << 8) | buf[off + 3];
		/* skip TTL at off+4 .. off+7 */
		rdlen = ((uint16_t)buf[off + 8] << 8) | buf[off + 9];
		off += 10;
		if (off + rdlen > n) {
			return;
		}

		if ((type == MDNS_TYPE_A) &&
		    ((cls & MDNS_CACHE_FLUSH_MASK) == MDNS_CLASS_IN) &&
		    (rdlen == 4)) {
			ip_addr_set_zero(&s_resp_addr);
			IP_SET_TYPE(&s_resp_addr, IPADDR_TYPE_V4);
			memcpy(&ip_2_ip4(&s_resp_addr)->addr, buf + off, 4);
			s_resp_id = id;
			s_resp_have = true;
			xSemaphoreGive(s_resp_sem);
			return;
		}

		off += rdlen;
	}
}

/*------------------------------------------------------------------*/

bool mdns_resolve_init(void)
{
	if (s_inited) {
		return true;
	}

	s_lock = xSemaphoreCreateMutex();
	s_resp_sem = xSemaphoreCreateBinary();
	if ((s_lock == NULL) || (s_resp_sem == NULL)) {
		fprintf(stderr, "mdns_resolve: failed to create semaphores\n");
		return false;
	}

	LOCK_TCPIP_CORE();
	s_pcb = udp_new();
	if (s_pcb == NULL) {
		UNLOCK_TCPIP_CORE();
		fprintf(stderr, "mdns_resolve: udp_new failed\n");
		return false;
	}
	/* Ephemeral source port; with the QU bit responses come back here
	 * unicast and we don't collide with lwIP's responder on 5353. */
	if (udp_bind(s_pcb, IP_ANY_TYPE, 0) != ERR_OK) {
		udp_remove(s_pcb);
		s_pcb = NULL;
		UNLOCK_TCPIP_CORE();
		fprintf(stderr, "mdns_resolve: udp_bind failed\n");
		return false;
	}
	/* RFC 6762 §11: mDNS frames must be sent with IP TTL=255; strict
	 * responders silently drop queries with smaller TTLs.  Set both the
	 * multicast-TX TTL (used for our query) and the unicast TTL. */
	udp_set_multicast_ttl(s_pcb, 255);
	s_pcb->ttl = 255;
	udp_recv(s_pcb, mdns_recv_cb, NULL);
	UNLOCK_TCPIP_CORE();

	s_inited = true;
	return true;
}

static int build_query(uint8_t *buf, int bufsize, uint16_t id,
		       const char *name)
{
	int off = 0;
	int name_len;

	if (bufsize < 17) { /* 12 hdr + at least 1+1+1+2+2 */
		return -1;
	}

	/* Header: caller-supplied ID; all flag bits zero; QDCOUNT=1,
	 * ANCOUNT/NSCOUNT/ARCOUNT=0.  The ID lets mdns_resolve distinguish a
	 * fresh answer from a late callback for an earlier query. */
	memset(buf, 0, 12);
	buf[0] = (uint8_t)(id >> 8);
	buf[1] = (uint8_t)(id & 0xFF);
	buf[5] = 1; /* QDCOUNT low byte */
	off = 12;

	name_len = encode_name(buf + off, bufsize - off, name);
	if (name_len < 0) {
		return -1;
	}
	off += name_len;

	if (off + 4 > bufsize) {
		return -1;
	}
	/* QTYPE = A (1) */
	buf[off++] = 0;
	buf[off++] = MDNS_TYPE_A;
	/* QCLASS = IN (1) with QU bit set in the high byte */
	buf[off++] = (uint8_t)((MDNS_QU_BIT >> 8) & 0xFF);
	buf[off++] = MDNS_CLASS_IN;

	return off;
}

bool mdns_resolve(const char *name, ip_addr_t *out, uint32_t timeout_ms)
{
	uint8_t	     pkt[MDNS_FRAME_MAX];
	ip_addr_t    dst;
	struct pbuf *p;
	int	     len;
	uint16_t     expected_id;
	bool	     got;

	if ((name == NULL) || (*name == '\0') || !s_inited) {
		return false;
	}

	xSemaphoreTake(s_lock, portMAX_DELAY);

	/* Drain any stale signal left by a late response from a previous
	 * call (the binary semaphore retains one give). */
	while (xSemaphoreTake(s_resp_sem, 0) == pdTRUE) {
		/* drain */
	}
	s_resp_have = false;

	/* Pick a non-zero ID; the recv callback echoes it back and we match
	 * after the take.  This closes the race where a delayed response to
	 * an earlier query lands between the drain and our send and would
	 * otherwise unblock the caller with stale data. */
	expected_id = s_next_id++;
	if (s_next_id == 0) {
		s_next_id = 1;
	}

	len = build_query(pkt, sizeof(pkt), expected_id, name);
	if (len < 0) {
		xSemaphoreGive(s_lock);
		return false;
	}

	p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
	if (p == NULL) {
		xSemaphoreGive(s_lock);
		return false;
	}
	pbuf_take(p, pkt, (u16_t)len);

	IP_ADDR4(&dst, 224, 0, 0, 251);
	LOCK_TCPIP_CORE();
	udp_sendto(s_pcb, p, &dst, MDNS_PORT);
	UNLOCK_TCPIP_CORE();
	pbuf_free(p);

	/* Loop on the semaphore so we ignore stale responses (e.g. a delayed
	 * unicast reply to a previous query that the drain missed): if the
	 * IDs don't match we restart the wait with the remaining timeout. */
	{
		TickType_t deadline =
			xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

		got = false;
		for (;;) {
			TickType_t now = xTaskGetTickCount();
			TickType_t left =
				(deadline > now) ? (deadline - now) : 0;

			if ((xSemaphoreTake(s_resp_sem, left) == pdTRUE) &&
			    s_resp_have && (s_resp_id == expected_id)) {
				got = true;
				break;
			}
			if (left == 0) {
				break;
			}
			s_resp_have = false; /* discard the unmatched signal */
		}
	}
	if (got && (out != NULL)) {
		*out = s_resp_addr;
	}

	xSemaphoreGive(s_lock);
	return got;
}
