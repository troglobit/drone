/*
 * mqtt_app.c - device-side MQTT client (lwIP apps/mqtt, raw API).
 *
 * Telemetry test patterns are published on dev/<id>/telemetry; commands are
 * received on dev/<id>/cmd and answered on dev/<id>/resp.  Supported commands:
 *   ping | status | rate <ms> | pattern <ramp|sine|random|const> |
 *   led <on|off> | reboot
 *
 * Broker address resolution (each connect attempt re-resolves so we follow
 * topology changes):
 *
 *   1. --broker as IPv4 dotted-decimal   -> parsed locally.
 *   2. --broker as *.local hostname       -> resolved via mdns_resolve()
 *                                            (one query per attempt, 2s
 *                                            timeout, retries indefinitely).
 *   3. no --broker                        -> wait for LLDP, take the
 *                                            neighbor's Management-Address
 *                                            TLV and port 1883.
 *
 * Each retry path keeps polling forever -- Linux/Infix peers can take
 * ~30 s to bring up lldpd and the mDNS responder, and we don't want to
 * fail prematurely on a slow upstream.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/apps/mqtt.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"

#include "lldp.h"
#include "mdns_resolve.h"
#include "mqtt_app.h"

#define TWO_PI 6.28318530718
#define SINE_PERIOD 64

enum pattern { PAT_RAMP, PAT_SINE, PAT_RANDOM, PAT_CONST };

static mqtt_client_t *s_client;
static struct mqtt_connect_client_info_t s_ci;
static ip_addr_t s_broker_ip;
static u16_t s_broker_port;
static char s_clientid[32];
static char s_broker_host[64]; /* explicit --broker host ("" if LLDP) */
static char s_wait_msg[96];    /* what we tell the operator we're waiting on */

static char s_topic_tel[64];
static char s_topic_cmd[64];
static char s_topic_resp[64];

static volatile int s_connected;
static volatile int s_rate_ms = 1000;
static volatile enum pattern s_pattern = PAT_RAMP;
static volatile int s_led;
static unsigned s_pub_count;

/* Incoming publish assembly (callbacks run in the tcpip thread). */
static char s_in_payload[256];
static u16_t s_in_len;

static const char *pattern_name(enum pattern p)
{
	switch (p) {
	case PAT_SINE:
		return "sine";
	case PAT_RANDOM:
		return "random";
	case PAT_CONST:
		return "const";
	default:
		return "ramp";
	}
}

static int pattern_value(enum pattern p, unsigned seq)
{
	switch (p) {
	case PAT_SINE:
		return (int)(128.0 + 100.0 * sin(TWO_PI * (seq % SINE_PERIOD) /
						 SINE_PERIOD));
	case PAT_RANDOM:
		return rand() & 0xff;
	case PAT_CONST:
		return 42;
	default:
		return (int)(seq & 0xff); /* ramp */
	}
}

/* Recognise a .local-style hostname.  Per RFC 6762 §16 mDNS comparisons
 * are case-insensitive, so the suffix check uses strncasecmp.  Also
 * tolerates a trailing FQDN dot ("broker1.local."), and validates each
 * label between the dots: non-empty, no leading/trailing hyphen, LDH
 * characters only.  Rejects empty labels ("..local"), leading dots
 * (".broker1.local"), and leading-hyphen labels ("-broker1.local") -- those
 * would otherwise pass the validator and then silently spin in the
 * wait loop because the on-wire mDNS encoder rejects them. */
static bool looks_like_local(const char *s)
{
	size_t n = strlen(s);
	const char suffix[] = ".local";
	const size_t slen = sizeof(suffix) - 1;
	bool in_label = false;
	char prev = '\0';
	size_t labels = 0;

	/* Drop a trailing FQDN dot if present. */
	if ((n > 0) && (s[n - 1] == '.')) {
		n--;
	}
	if ((n < slen + 1) ||
	    (strncasecmp(s + n - slen, suffix, slen) != 0)) {
		return false;
	}

	/* Validate every label up to (not including) ".local". */
	for (size_t i = 0; i < n - slen; i++) {
		char c = s[i];
		if (c == '.') {
			if (!in_label || (prev == '-')) {
				return false;
			}
			labels++;
			in_label = false;
			prev = '\0';
		} else {
			bool alnum = ((c >= 'a') && (c <= 'z')) ||
				     ((c >= 'A') && (c <= 'Z')) ||
				     ((c >= '0') && (c <= '9'));
			if (!alnum && (c != '-')) {
				return false;
			}
			if (!in_label && (c == '-')) {
				return false; /* leading hyphen */
			}
			in_label = true;
			prev = c;
		}
	}
	if (!in_label || (prev == '-')) {
		return false;
	}
	labels++;

	return labels > 0;
}

/* Publish a reply on dev/<id>/resp.  Called from the tcpip thread (incoming
 * data callback), where the core lock is already held. */
static void publish_resp(const char *msg)
{
	mqtt_publish(s_client, s_topic_resp, msg, (u16_t)strlen(msg), 0, 0,
		     NULL, NULL);
}

static void handle_command(char *cmd)
{
	char resp[192];

	/* trim trailing whitespace */
	size_t n = strlen(cmd);
	while (n && (cmd[n - 1] == '\n' || cmd[n - 1] == '\r' ||
		     cmd[n - 1] == ' ')) {
		cmd[--n] = '\0';
	}

	printf("mqtt: command \"%s\"\n", cmd);

	if (strcmp(cmd, "ping") == 0) {
		publish_resp("pong");
	} else if (strcmp(cmd, "status") == 0) {
		snprintf(
			resp, sizeof resp,
			"uptime=%lums rate=%dms pattern=%s led=%s published=%u",
			(unsigned long)(xTaskGetTickCount() *
					portTICK_PERIOD_MS),
			s_rate_ms, pattern_name(s_pattern),
			s_led ? "on" : "off", s_pub_count);
		publish_resp(resp);
	} else if (strncmp(cmd, "rate ", 5) == 0) {
		int v = atoi(cmd + 5);
		if (v >= 10 && v <= 60000) {
			s_rate_ms = v;
			snprintf(resp, sizeof resp, "rate=%dms", v);
		} else {
			snprintf(resp, sizeof resp,
				 "error: rate out of range (10..60000)");
		}
		publish_resp(resp);
	} else if (strncmp(cmd, "pattern ", 8) == 0) {
		const char *a = cmd + 8;
		if (strcmp(a, "ramp") == 0) {
			s_pattern = PAT_RAMP;
		} else if (strcmp(a, "sine") == 0) {
			s_pattern = PAT_SINE;
		} else if (strcmp(a, "random") == 0) {
			s_pattern = PAT_RANDOM;
		} else if (strcmp(a, "const") == 0) {
			s_pattern = PAT_CONST;
		} else {
			publish_resp("error: unknown pattern");
			return;
		}
		snprintf(resp, sizeof resp, "pattern=%s",
			 pattern_name(s_pattern));
		publish_resp(resp);
	} else if (strncmp(cmd, "led ", 4) == 0) {
		if (strcmp(cmd + 4, "on") == 0) {
			s_led = 1;
		} else if (strcmp(cmd + 4, "off") == 0) {
			s_led = 0;
		} else {
			publish_resp("error: led expects on|off");
			return;
		}
		snprintf(resp, sizeof resp, "led=%s", s_led ? "on" : "off");
		publish_resp(resp);
	} else if (strcmp(cmd, "reboot") == 0) {
		/* No-op in the simulator; the real device would reset here. */
		publish_resp("reboot ack (noop in simulator)");
	} else {
		snprintf(resp, sizeof resp, "error: unknown command");
		publish_resp(resp);
	}
}

static void incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
	LWIP_UNUSED_ARG(arg);
	LWIP_UNUSED_ARG(topic);
	LWIP_UNUSED_ARG(tot_len);
	s_in_len = 0;
}

static void incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	LWIP_UNUSED_ARG(arg);

	if (s_in_len + len < sizeof(s_in_payload)) {
		memcpy(s_in_payload + s_in_len, data, len);
		s_in_len += len;
	}

	if (flags & MQTT_DATA_FLAG_LAST) {
		s_in_payload[s_in_len] = '\0';
		handle_command(s_in_payload);
		s_in_len = 0;
	}
}

static void sub_cb(void *arg, err_t err)
{
	LWIP_UNUSED_ARG(arg);
	printf("mqtt: subscribe %s -> %s\n", s_topic_cmd,
	       (err == ERR_OK) ? "ok" : "failed");
}

static void connection_cb(mqtt_client_t *client, void *arg,
			  mqtt_connection_status_t status)
{
	LWIP_UNUSED_ARG(arg);

	if (status == MQTT_CONNECT_ACCEPTED) {
		printf("mqtt: connected, publishing on %s\n", s_topic_tel);
		mqtt_set_inpub_callback(client, incoming_publish_cb,
					incoming_data_cb, NULL);
		mqtt_subscribe(client, s_topic_cmd, 0, sub_cb, NULL);
		s_connected = 1;
	} else {
		printf("mqtt: disconnected/refused (status=%d)\n", (int)status);
		s_connected = 0;
	}
}

/* Resolve the broker IP for the next connect attempt:
 *   - explicit IPv4 in --broker         -> parse directly
 *   - explicit *.local hostname         -> mDNS query (2s timeout)
 *   - no --broker                       -> LLDP neighbor mgmt-addr
 * Each call sends at most one mDNS query packet; the wait loop in
 * mqtt_task supplies the retry cadence. */
static bool resolve_broker_ip(ip_addr_t *out)
{
	if (s_broker_host[0] == '\0') {
		return lldp_get_neighbor_addr(out);
	}
	if (ip4addr_aton(s_broker_host, ip_2_ip4(out))) {
		IP_SET_TYPE(out, IPADDR_TYPE_V4);
		return true;
	}
	/* .local hostname: one-shot mDNS A-record query.  Slow Linux/Infix
	 * peers take ~30s to come up; we just keep polling until they do. */
	return mdns_resolve(s_broker_host, out, 2000);
}

static void mqtt_task(void *arg)
{
	unsigned seq = 0;
	int wait_ticks = 0;

	LWIP_UNUSED_ARG(arg);

	s_client = mqtt_client_new();
	if (s_client == NULL) {
		printf("mqtt: mqtt_client_new failed\n");
		vTaskDelete(NULL);
		return;
	}

	/* Wait for a broker address.  IPv4 in --broker resolves instantly;
	 * LLDP / mDNS may take a while if the upstream is a slow Linux peer.
	 * The message in s_wait_msg was precomputed in mqtt_app_start so the
	 * operator can see WHICH mechanism is blocking. */
	while (!resolve_broker_ip(&s_broker_ip)) {
		if ((wait_ticks % 10) == 0) {
			printf("drone: %s\n", s_wait_msg);
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
		wait_ticks++;
	}

	for (;;) {
		if (!s_connected) {
			err_t e;

			/* Re-resolve on each attempt so we follow neighbor or
			 * mDNS changes.  resolve_broker_ip may return false on
			 * a transient mDNS / LLDP hiccup; in that case fall
			 * back to the last known good address. */
			resolve_broker_ip(&s_broker_ip);
			printf("mqtt: connecting to %s:%u as \"%s\"\n",
			       ipaddr_ntoa(&s_broker_ip), s_broker_port,
			       s_clientid);

			LOCK_TCPIP_CORE();
			e = mqtt_client_connect(s_client, &s_broker_ip,
						s_broker_port, connection_cb,
						NULL, &s_ci);
			UNLOCK_TCPIP_CORE();

			if (e != ERR_OK) {
				printf("mqtt: connect call failed (%d), "
				       "retrying\n",
				       e);
			}

			/* Wait up to ~3 s for the CONNACK. */
			for (int i = 0; (i < 30) && !s_connected; i++) {
				vTaskDelay(pdMS_TO_TICKS(100));
			}

			if (!s_connected) {
				vTaskDelay(pdMS_TO_TICKS(2000));
				continue;
			}
		}

		{
			char payload[160];
			int val = pattern_value(s_pattern, seq);
			int n = snprintf(payload, sizeof payload,
					 "{\"seq\":%u,\"val\":%d,\"pattern\":"
					 "\"%s\",\"led\":%d}",
					 seq, val, pattern_name(s_pattern),
					 s_led);
			err_t e;

			LOCK_TCPIP_CORE();
			e = mqtt_publish(s_client, s_topic_tel, payload,
					 (u16_t)n, 0, 0, NULL, NULL);
			UNLOCK_TCPIP_CORE();

			if (e == ERR_OK) {
				s_pub_count++;
			} else if ((e == ERR_CONN) ||
				   !mqtt_client_is_connected(s_client)) {
				s_connected = 0;
			}
			seq++;
		}

		vTaskDelay(pdMS_TO_TICKS(s_rate_ms));
	}
}

void mqtt_app_start(const struct app_cfg *cfg)
{
	bool is_ipv4 = false;
	bool is_local = false;

	/* Up-front validation so a typo doesn't silently disappear into the
	 * wait loop and look like a discovery problem. */
	if (cfg->broker_host[0] != '\0') {
		ip_addr_t tmp;
		is_ipv4 = ip4addr_aton(cfg->broker_host, ip_2_ip4(&tmp));
		is_local = looks_like_local(cfg->broker_host);
		if (!is_ipv4 && !is_local) {
			printf("mqtt: bad --broker '%s' (need IPv4 or "
			       "*.local hostname)\n",
			       cfg->broker_host);
			return;
		}
	} else if (!cfg->lldp) {
		/* No --broker and no --lldp: there's no discovery channel.
		 * Refuse to start rather than spin the wait loop forever
		 * looking for an LLDP frame that will never arrive. */
		printf("mqtt: no --broker given and --lldp not set; "
		       "broker discovery requires LLDP\n");
		return;
	}

	snprintf(s_broker_host, sizeof s_broker_host, "%s", cfg->broker_host);
	s_broker_port = (u16_t)cfg->broker_port;

	/* Compose the wait-loop message once; the mqtt_task just prints it. */
	if (s_broker_host[0] == '\0') {
		snprintf(s_wait_msg, sizeof s_wait_msg,
			 "waiting for LLDP from neighboring MQTT broker");
	} else if (is_local) {
		snprintf(s_wait_msg, sizeof s_wait_msg,
			 "waiting for mDNS to resolve %s", s_broker_host);
	} else {
		/* IPv4: resolve_broker_ip will succeed immediately and the
		 * wait loop won't run, but seed something readable just in
		 * case (e.g. a future netif-down transient). */
		snprintf(s_wait_msg, sizeof s_wait_msg,
			 "waiting for netif to come up");
	}

	snprintf(s_clientid, sizeof s_clientid, "%s", cfg->hostname);
	snprintf(s_topic_tel, sizeof s_topic_tel, "dev/%s/telemetry",
		 cfg->hostname);
	snprintf(s_topic_cmd, sizeof s_topic_cmd, "dev/%s/cmd", cfg->hostname);
	snprintf(s_topic_resp, sizeof s_topic_resp, "dev/%s/resp",
		 cfg->hostname);

	s_ci.client_id = s_clientid;
	s_ci.keep_alive = 30;

	xTaskCreate(mqtt_task, "mqtt", configMINIMAL_STACK_SIZE * 6, NULL,
		    tskIDLE_PRIORITY + 2, NULL);
}
