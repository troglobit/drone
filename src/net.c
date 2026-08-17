/*
 * net.c - argument parsing and lwIP/interface bring-up.
 */
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/autoip.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"

#include "net.h"
#include "version.h"
#include "ping.h"
#include "lldp.h"
#include "qeneth_netif.h"

static struct netif s_netif;

void app_cfg_defaults(struct app_cfg *c)
{
	memset(c, 0, sizeof(*c));
	strcpy(c->localaddr, "127.0.0.1:20000");
	strcpy(c->peeraddr, "127.0.0.1:20001");
	/* No --ip default: when omitted, the drone claims a 169.254/16 address
	 * via AutoIP (RFC 3927).  netmask/gw apply only when --ip is given. */
	strcpy(c->netmask, "255.255.255.0");
	strcpy(c->gw, "10.0.0.1");
	/* Locally administered unicast MAC (02:..).  --hostname stays empty so
	 * app_cfg_finalize() can derive drone-XXYYZZ from the low MAC bytes. */
	c->mac[0] = 0x02;
	c->mac[1] = 0x00;
	c->mac[2] = 0x00;
	c->mac[3] = 0x00;
	c->mac[4] = 0x00;
	c->mac[5] = 0x02;
	/* No broker_host default: when --broker is omitted, mqtt_app waits
	 * for the broker address to arrive via LLDP. */
	c->broker_port = 1883;
	c->lldp_interval = 30;
	c->lldp_ttl = 120;
	c->ping_count = 4;
}

static int parse_mac(const char *s, uint8_t mac[6])
{
	unsigned v[6];

	if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4],
		   &v[5]) != 6) {
		return -1;
	}

	for (int i = 0; i < 6; i++) {
		mac[i] = (uint8_t)v[i];
	}

	return 0;
}

static void usage(FILE *fp, const char *argv0)
{
	fprintf(fp,
		"usage: %s [options]\n"
		"      --localaddr HOST:PORT  bind the link UDP socket here "
		"(default 127.0.0.1:20000)\n"
		"      --udp HOST:PORT        send frames to this peer "
		"(default 127.0.0.1:20001)\n"
		"      --ip ADDR              static IPv4 address (omit -> "
		"AutoIP, or --dhcp)\n"
		"      --dhcp                 request a DHCP lease; AutoIP "
		"fallback if no server\n"
		"      --netmask ADDR         (default 255.255.255.0, used "
		"with --ip)\n"
		"      --gw ADDR              default gateway (default "
		"10.0.0.1, used with --ip)\n"
		"      --mac XX:XX:XX:XX:XX:XX  interface MAC (default "
		"02:00:00:00:00:02)\n"
		"      --hostname NAME        device id / role (default: "
		"drone-XXYYZZ from MAC)\n"
		"      --broker ADDR[:PORT]   MQTT broker as IPv4 or *.local "
		"name; without this, the\n"
		"                             address is discovered from a "
		"neighbor's LLDP TLV\n"
		"      --lldp                 enable LLDP TX/RX; gate "
		"--dhcp start on first neighbor\n"
		"                             frame; discover MQTT broker "
		"from mgmt-address TLV\n"
		"      --lldp-interval SECS   LLDP TX cadence (default 30)\n"
		"      --lldp-ttl SECS        LLDP TTL we advertise (default "
		"120)\n"
		"      --run-broker           act as the built-in test broker "
		"instead\n"
		"      --no-mqtt              skip the MQTT client "
		"(diagnostics only)\n"
		"  -p, --ping ADDR            send ICMP echo requests after "
		"bring-up\n"
		"  -n, --ping-count NUM       echo-request count (default "
		"4)\n"
		"  -h, --help                 this help\n"
		"  -v, --version              show version\n",
		argv0);
}

static void version(void)
{
	printf("drone %s\n", DRONE_VERSION);
	printf("FreeRTOS %s, lwIP %s\n", tskKERNEL_VERSION_NUMBER,
	       LWIP_VERSION_STRING);
}

/* Long-only option keys.  Values >= 256 stay out of the short-option char
 * range so getopt_long returns them directly via its int return. */
enum {
	OPT_LOCALADDR = 0x100,
	OPT_UDP,
	OPT_IP,
	OPT_NETMASK,
	OPT_GW,
	OPT_MAC,
	OPT_HOSTNAME,
	OPT_DHCP,
	OPT_BROKER,
	OPT_LLDP,
	OPT_LLDP_INTERVAL,
	OPT_LLDP_TTL,
	OPT_RUN_BROKER,
	OPT_NO_MQTT,
};

enum cfg_result app_cfg_parse(struct app_cfg *c, int argc, char **argv)
{
	static const struct option long_opts[] = {
		{ "localaddr",	   required_argument, NULL, OPT_LOCALADDR },
		{ "udp",	   required_argument, NULL, OPT_UDP },
		{ "ip",		   required_argument, NULL, OPT_IP },
		{ "netmask",	   required_argument, NULL, OPT_NETMASK },
		{ "gw",		   required_argument, NULL, OPT_GW },
		{ "mac",	   required_argument, NULL, OPT_MAC },
		{ "hostname",	   required_argument, NULL, OPT_HOSTNAME },
		{ "dhcp",	   no_argument,	      NULL, OPT_DHCP },
		{ "broker",	   required_argument, NULL, OPT_BROKER },
		{ "lldp",	   no_argument,	      NULL, OPT_LLDP },
		{ "lldp-interval", required_argument, NULL, OPT_LLDP_INTERVAL },
		{ "lldp-ttl",	   required_argument, NULL, OPT_LLDP_TTL },
		{ "run-broker",	   no_argument,	      NULL, OPT_RUN_BROKER },
		{ "no-mqtt",	   no_argument,	      NULL, OPT_NO_MQTT },
		{ "ping",	   required_argument, NULL, 'p' },
		{ "ping-count",	   required_argument, NULL, 'n' },
		{ "help",	   no_argument,	      NULL, 'h' },
		{ "version",	   no_argument,	      NULL, 'v' },
		{ 0, 0, 0, 0 }
	};
	int opt;

	/* Allow the function to be called repeatedly in tests / future code:
	 * glibc keeps optind across calls otherwise. */
	optind = 1;

	while ((opt = getopt_long(argc, argv, "hvp:n:", long_opts, NULL)) !=
	       -1) {
		switch (opt) {
		case OPT_LOCALADDR:
			snprintf(c->localaddr, sizeof c->localaddr, "%s",
				 optarg);
			break;
		case OPT_UDP:
			snprintf(c->peeraddr, sizeof c->peeraddr, "%s", optarg);
			break;
		case OPT_IP:
			snprintf(c->ip, sizeof c->ip, "%s", optarg);
			break;
		case OPT_NETMASK:
			snprintf(c->netmask, sizeof c->netmask, "%s", optarg);
			break;
		case OPT_GW:
			snprintf(c->gw, sizeof c->gw, "%s", optarg);
			break;
		case OPT_MAC:
			if (parse_mac(optarg, c->mac) != 0) {
				fprintf(stderr, "bad --mac\n");
				return CFG_ERROR;
			}
			break;
		case OPT_HOSTNAME:
			snprintf(c->hostname, sizeof c->hostname, "%s", optarg);
			break;
		case OPT_DHCP:
			c->dhcp = 1;
			break;
		case OPT_BROKER: {
			char *colon = strrchr(optarg, ':');

			if (colon != NULL) {
				*colon = '\0';
				c->broker_port = atoi(colon + 1);
			}
			snprintf(c->broker_host, sizeof c->broker_host, "%s",
				 optarg);
			break;
		}
		case OPT_LLDP:
			c->lldp = 1;
			break;
		case OPT_LLDP_INTERVAL:
			c->lldp_interval = atoi(optarg);
			if (c->lldp_interval < 1) {
				fprintf(stderr,
					"--lldp-interval must be >= 1 (got '%s')\n",
					optarg);
				return CFG_ERROR;
			}
			break;
		case OPT_LLDP_TTL:
			c->lldp_ttl = atoi(optarg);
			if (c->lldp_ttl < 1) {
				fprintf(stderr,
					"--lldp-ttl must be >= 1 (got '%s')\n",
					optarg);
				return CFG_ERROR;
			}
			break;
		case OPT_RUN_BROKER:
			c->is_broker = 1;
			break;
		case OPT_NO_MQTT:
			c->no_mqtt = 1;
			break;
		case 'p':
			snprintf(c->ping_target, sizeof c->ping_target, "%s",
				 optarg);
			c->do_ping = 1;
			break;
		case 'n':
			c->ping_count = atoi(optarg);
			break;
		case 'h':
			usage(stdout, argv[0]);
			return CFG_DONE;
		case 'v':
			version();
			return CFG_DONE;
		default:
			/* getopt_long already printed a diagnostic for
			 * unknown options and missing required arguments. */
			usage(stderr, argv[0]);
			return CFG_ERROR;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "unexpected argument: %s\n", argv[optind]);
		usage(stderr, argv[0]);
		return CFG_ERROR;
	}

	return CFG_OK;
}

void app_cfg_finalize(struct app_cfg *c)
{
	/* When --hostname wasn't given, derive a unique-by-MAC default so two
	 * unconfigured drones don't collide on the same name. */
	if (c->hostname[0] == '\0') {
		snprintf(c->hostname, sizeof c->hostname, "drone-%02x%02x%02x",
			 c->mac[3], c->mac[4], c->mac[5]);
	}

	/* --ip overrides any address-acquisition mechanism; --dhcp is moot
	 * if the address is already fixed. */
	if ((c->ip[0] != '\0') && c->dhcp) {
		fprintf(stderr,
			"drone: --ip and --dhcp are mutually exclusive; "
			"ignoring --dhcp\n");
		c->dhcp = 0;
	}
}

/* Split "host:port" (host optional, defaults to 127.0.0.1) into a sockaddr. */
static int parse_hostport(const char *s, struct sockaddr_in *sa)
{
	char host[64];
	const char *colon = strrchr(s, ':');
	int port;

	if (colon == NULL) {
		return -1;
	}

	port = atoi(colon + 1);
	if ((colon - s) >= (long)sizeof(host)) {
		return -1;
	}
	memcpy(host, s, colon - s);
	host[colon - s] = '\0';

	memset(sa, 0, sizeof(*sa));
	sa->sin_family = AF_INET;
	sa->sin_port = htons((uint16_t)port);

	/* qeneth links use "localhost"; accept it (and an empty host) as
	 * loopback. inet_addr() only understands dotted-decimal, so map the
	 * name here. */
	if ((host[0] == '\0') || (strcmp(host, "localhost") == 0)) {
		sa->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	} else {
		sa->sin_addr.s_addr = inet_addr(host);
		if (sa->sin_addr.s_addr == INADDR_NONE) {
			return -1;
		}
	}
	return 0;
}

static int open_link_socket(const char *localaddr)
{
	struct sockaddr_in local;
	int fd, on = 1;

	if (parse_hostport(localaddr, &local) != 0) {
		fprintf(stderr, "bad --localaddr '%s'\n", localaddr);
		return -1;
	}

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	if (bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
		fprintf(stderr, "bind %s: %s\n", localaddr, strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}

/* Fired (from the LLDP RX-task context) when the first neighbor LLDPDU
 * arrives, gating the DHCP DISCOVER on "the upstream is at least alive
 * enough to send LLDP".  Fires exactly once because lldp.c latches
 * s_any_neighbor_seen on first frame -- important, because lwIP's
 * dhcp_start() on a netif that already has DHCP running tears it down
 * and restarts (memset + DISCOVER), so calling it twice would blow
 * away an established lease. */
static void start_dhcp_on_lldp(void)
{
	LOCK_TCPIP_CORE();
	dhcp_start(&s_netif);
	UNLOCK_TCPIP_CORE();
	/* Log after releasing the core lock: printf can briefly block on
	 * a piped stdout and we don't want to stall the tcpip thread (or
	 * the qn_rx_task that's running us) any longer than needed. */
	printf("drone: LLDP neighbor seen, starting DHCP\n");
}

/* lwIP fires this callback when the netif's IPv4 address becomes valid;
 * with AutoIP, that's after the probe-and-claim cycle completes. */
NETIF_DECLARE_EXT_CALLBACK(s_ext_cb)
static void on_netif_ext(struct netif *nif, netif_nsc_reason_t reason,
			 const netif_ext_callback_args_t *args)
{
	LWIP_UNUSED_ARG(args);

	if (reason &
	    (LWIP_NSC_IPV4_ADDR_VALID | LWIP_NSC_IPV4_ADDRESS_CHANGED)) {
		const ip4_addr_t *a = netif_ip4_addr(nif);
		if (!ip4_addr_isany(a)) {
			printf("drone: ipv4 address: %s\n", ip4addr_ntoa(a));
		}
	}
#if LWIP_IPV6
	if (reason & LWIP_NSC_IPV6_ADDR_STATE_CHANGED) {
		s8_t idx = args->ipv6_addr_state_changed.addr_index;
		if (ip6_addr_isvalid(netif_ip6_addr_state(nif, idx))) {
			/* lwIP's ip6addr_ntoa() predates RFC 5952 and emits
			 * uppercase hex; downcase here so logs match what every
			 * other tool prints. */
			char buf[48];
			char *p;
			snprintf(buf, sizeof buf, "%s",
				 ip6addr_ntoa(netif_ip6_addr(nif, idx)));
			for (p = buf; *p != '\0'; p++) {
				*p = (char)tolower((unsigned char)*p);
			}
			printf("drone: ipv6 address: %s\n", buf);
		}
	}
#endif
}

int net_start(const struct app_cfg *c)
{
	ip4_addr_t ip, mask, gw;
	struct sockaddr_in peer;
	int autoip = (c->ip[0] == '\0');
	int fd;

	if (autoip) {
		ip4_addr_set_zero(&ip);
		ip4_addr_set_zero(&mask);
		ip4_addr_set_zero(&gw);
	} else if (!ip4addr_aton(c->ip, &ip) ||
		   !ip4addr_aton(c->netmask, &mask) ||
		   !ip4addr_aton(c->gw, &gw)) {
		fprintf(stderr, "drone: bad IP configuration\n");
		return -1;
	}

	if (parse_hostport(c->peeraddr, &peer) != 0) {
		fprintf(stderr, "drone: bad --udp '%s'\n", c->peeraddr);
		return -1;
	}

	fd = open_link_socket(c->localaddr);
	if (fd < 0) {
		return -1;
	}

	printf("drone: link bind=%s peer=%s "
	       "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
	       c->localaddr, c->peeraddr, c->mac[0], c->mac[1], c->mac[2],
	       c->mac[3], c->mac[4], c->mac[5]);

	/* Bring up lwIP.  tcpip_init() creates the core-lock mutex
	 * synchronously and spawns the tcpip thread. */
	tcpip_init(NULL, NULL);

	LOCK_TCPIP_CORE();
	netif_add_ext_callback(&s_ext_cb, on_netif_ext);
	UNLOCK_TCPIP_CORE();

	/* Pre-register the LLDP-gated DHCP trigger BEFORE qeneth_netif_add
	 * spawns qn_rx_task, so a peer's first 802.1AB frame can't slip
	 * past the dispatch loop with a NULL callback slot. */
	if (c->dhcp && c->lldp) {
		lldp_on_neighbor_seen(start_dhcp_on_lldp);
	}

	/* Pass --hostname through to the netif init path so netif->hostname
	 * is set before qeneth_netif_add returns (and before its RX task can
	 * dispatch a frame that fires start_dhcp_on_lldp).  lwIP's DHCP
	 * client emits the name as option 12; static leases keyed on it
	 * (dnsmasq dhcp-host=NAME,IP) then identify the drone. */
	if (qeneth_netif_add(&s_netif, &ip, &mask, &gw, c->mac, c->hostname,
			     fd, peer.sin_addr.s_addr,
			     peer.sin_port) != ERR_OK) {
		fprintf(stderr, "drone: failed to add interface\n");
		close(fd);
		return -1;
	}

	if (c->dhcp && c->lldp) {
		/* Don't burn DHCP retries before the upstream Linux peer is
		 * up; lldp_on_neighbor_seen (registered above) will call
		 * dhcp_start the first time we hear a neighbor LLDPDU. */
		printf("drone: interface up (DHCP, gated on LLDP "
		       "neighbor)\n");
	} else if (c->dhcp) {
		/* No LLDP gating: start DHCP immediately.  RFC 3927
		 * cooperative AutoIP fallback applies after
		 * LWIP_DHCP_AUTOIP_COOP_TRIES failed DISCOVERs. */
		LOCK_TCPIP_CORE();
		dhcp_start(&s_netif);
		UNLOCK_TCPIP_CORE();
		printf("drone: interface up (DHCP%s), awaiting lease...\n",
		       LWIP_DHCP_AUTOIP_COOP ? " + AutoIP fallback" : "");
	} else if (autoip) {
		LOCK_TCPIP_CORE();
		autoip_start(&s_netif);
		UNLOCK_TCPIP_CORE();
		printf("drone: interface up (AutoIP), awaiting claim...\n");
	} else {
		printf("drone: interface up, ip=%s/%s gw=%s\n", c->ip,
		       c->netmask, c->gw);
	}

	if (c->do_ping) {
		ip4_addr_t target;

		if (ip4addr_aton(c->ping_target, &target)) {
			ping_start(&target, c->ping_count);
		} else {
			fprintf(stderr, "drone: bad --ping target '%s'\n",
				c->ping_target);
		}
	}

	return 0;
}

struct netif *net_netif(void)
{
	return &s_netif;
}
