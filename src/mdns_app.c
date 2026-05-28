/*
 * mdns_app.c - announce <hostname>.local and a _drone._tcp service.
 *
 * The CNC can browse `_drone._tcp.local` (e.g. with avahi-browse) to find
 * every drone on the lab L2, then resolve <hostname>.local to its address.
 * TXT records carry the device id, firmware tag, and current broker.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/apps/mdns.h"

#include "mdns_app.h"

static char s_hostname[32];
static char s_broker[24];

/* Format a single TXT item into a bounded buffer and emit it, clamping the
 * length so we never hand mDNS more bytes than were actually written. */
static void add_txt(struct mdns_service *service, const char *fmt, ...)
{
	char item[64];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(item, sizeof item, fmt, ap);
	va_end(ap);

	/* n <= 0 covers the encoding-error case (-1) and the empty-string case
	 * (a zero-length TXT item terminates the TXT chain and would silently
	 * drop every item added after it). */
	if (n <= 0) {
		return;
	}
	if (n >= (int)sizeof item) {
		n = (int)sizeof item - 1;
	}
	mdns_resp_add_service_txtitem(service, item, (u8_t)n);
}

/* Called by the responder when it needs to emit our service's TXT records.
 * Runs in the tcpip thread (core lock held). */
static void srv_txt(struct mdns_service *service, void *arg)
{
	(void)arg;

	add_txt(service, "id=%s", s_hostname);
	add_txt(service, "fw=drone-dev");

	if (s_broker[0] != '\0') {
		add_txt(service, "mqtt=%s", s_broker);
	}
}

void mdns_app_start(const struct app_cfg *cfg)
{
	struct netif *nif = netif_default;
	err_t err;
	s8_t slot;

	if (nif == NULL) {
		printf("mdns: no default netif, responder not started\n");
		return;
	}

	snprintf(s_hostname, sizeof s_hostname, "%s", cfg->hostname);

	/* Only advertise the broker if we'll actually be using it; with
	 * --no-mqtt the static buffer stays empty and srv_txt skips the mqtt=
	 * TXT. */
	if (!cfg->no_mqtt) {
		snprintf(s_broker, sizeof s_broker, "%s:%d", cfg->broker_ip,
			 cfg->broker_port);
	}

	LOCK_TCPIP_CORE();
	mdns_resp_init();
	err = mdns_resp_add_netif(nif, s_hostname);
	slot = -1;
	if (err == ERR_OK) {
		slot = mdns_resp_add_service(
			nif, s_hostname, "_drone", DNSSD_PROTO_TCP,
			(u16_t)cfg->broker_port, srv_txt, NULL);
		if (slot < 0) {
			/* Keep responder state consistent: the netif was
			 * registered but the service slot wasn't, so a later
			 * retry would otherwise trip lwIP's 'Double add'
			 * assert. */
			mdns_resp_remove_netif(nif);
		}
	}
	UNLOCK_TCPIP_CORE();

	if ((err != ERR_OK) || (slot < 0)) {
		printf("mdns: registration failed (netif=%d service=%d)\n",
		       (int)err, (int)slot);
		return;
	}

	printf("mdns: announcing %s.local + _drone._tcp\n", s_hostname);
}
