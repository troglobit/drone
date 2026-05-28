/*
 * test_broker.c - a *minimal* single-client MQTT broker, built on lwIP raw
 * TCP, used as a self-contained test fixture (run with --broker).  It is NOT a
 * real broker: it accepts one client, ACKs CONNECT/SUBSCRIBE/PING, prints the
 * device's telemetry/responses, and drives a fixed command sequence.  In the
 * real lab the device talks to mosquitto on an Infix node instead.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/tcp.h"
#include "lwip/tcpip.h"

#include "test_broker.h"

#define BROKER_PORT 1883

static struct tcp_pcb *s_listen;
static struct tcp_pcb *s_conn; /* single connected client */
static uint8_t s_rx[2048];
static size_t s_rxlen;
static char s_dev_id[32];
static int s_cmd_started;

/* --- transmit helpers (caller holds the core lock) ----------------------- */

static void tcp_send(const void *data, u16_t len)
{
	if ((s_conn != NULL) &&
	    (tcp_write(s_conn, data, len, TCP_WRITE_FLAG_COPY) == ERR_OK)) {
		tcp_output(s_conn);
	}
}

/* MQTT remaining-length varint encoder; returns bytes written (1..4). */
static int encode_remlen(uint8_t *out, size_t len)
{
	int i = 0;

	do {
		uint8_t b = len & 0x7f;
		len >>= 7;
		if (len > 0) {
			b |= 0x80;
		}
		out[i++] = b;
	} while (len > 0);

	return i;
}

static void send_publish(const char *topic, const char *payload)
{
	uint8_t pkt[320];
	size_t tlen = strlen(topic);
	size_t plen = strlen(payload);
	size_t rl =
		2 + tlen + plen; /* topic-len field + topic + payload (QoS0) */
	int i = 0, rlbytes;

	if ((1 + 4 + rl) > sizeof(pkt)) {
		return;
	}

	pkt[i++] = 0x30; /* PUBLISH, QoS 0 */
	rlbytes = encode_remlen(&pkt[i], rl);
	i += rlbytes;
	pkt[i++] = (uint8_t)(tlen >> 8);
	pkt[i++] = (uint8_t)(tlen & 0xff);
	memcpy(&pkt[i], topic, tlen);
	i += tlen;
	memcpy(&pkt[i], payload, plen);
	i += plen;

	tcp_send(pkt, (u16_t)i);
}

/* Pull the <id> out of "dev/<id>/..." */
static void learn_dev_id(const char *topic)
{
	const char *a, *b;

	if (s_dev_id[0] != '\0') {
		return;
	}
	if (strncmp(topic, "dev/", 4) != 0) {
		return;
	}

	a = topic + 4;
	b = strchr(a, '/');
	if ((b != NULL) && ((b - a) < (long)sizeof(s_dev_id))) {
		memcpy(s_dev_id, a, b - a);
		s_dev_id[b - a] = '\0';
		printf("test-broker: device id = \"%s\"\n", s_dev_id);
	}
}

static void start_command_sequence(void);

static void handle_packet(uint8_t type, uint8_t flags, const uint8_t *p,
			  size_t len)
{
	switch (type) {
	case 1: /* CONNECT */
	{
		const uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
		printf("test-broker: <- CONNECT\n");
		tcp_send(connack, sizeof connack);
		break;
	}

	case 3: /* PUBLISH */
	{
		u16_t tlen = (u16_t)((p[0] << 8) | p[1]);
		u8_t qos = (flags >> 1) & 0x03;
		const char *topic = (const char *)(p + 2);
		char tbuf[64];
		const uint8_t *data;
		size_t dlen, off = 2 + tlen;

		if (qos > 0) {
			off += 2; /* packet id */
		}
		data = p + off;
		dlen = (len > off) ? (len - off) : 0;

		snprintf(tbuf, sizeof tbuf, "%.*s", tlen, topic);
		printf("test-broker: <- [%s] %.*s\n", tbuf, (int)dlen, data);

		learn_dev_id(tbuf);
		if (!s_cmd_started && (s_dev_id[0] != '\0')) {
			s_cmd_started = 1;
			start_command_sequence();
		}
		break;
	}

	case 8: /* SUBSCRIBE */
	{
		uint8_t suback[5];
		u16_t pid = (u16_t)((p[0] << 8) | p[1]);
		printf("test-broker: <- SUBSCRIBE (id=%u)\n", pid);
		suback[0] = 0x90;
		suback[1] = 0x03;
		suback[2] = p[0];
		suback[3] = p[1];
		suback[4] = 0x00; /* granted QoS 0 */
		tcp_send(suback, sizeof suback);
		break;
	}

	case 12: /* PINGREQ */
	{
		const uint8_t pingresp[] = {0xD0, 0x00};
		tcp_send(pingresp, sizeof pingresp);
		break;
	}

	case 14: /* DISCONNECT */
		printf("test-broker: <- DISCONNECT\n");
		break;

	default:
		break;
	}
}

/* Extract whole MQTT packets from the accumulation buffer. */
static void parse_packets(void)
{
	size_t off = 0;

	while ((s_rxlen - off) >= 2) {
		uint8_t type = s_rx[off] >> 4;
		uint8_t flags = s_rx[off] & 0x0f;
		size_t rl = 0, mult = 1, i = off + 1, nbytes = 0;
		int done = 0;

		while (i < s_rxlen) {
			uint8_t b = s_rx[i++];
			rl += (size_t)(b & 0x7f) * mult;
			mult *= 128;
			nbytes++;
			if (!(b & 0x80)) {
				done = 1;
				break;
			}
			if (nbytes >= 4) {
				s_rxlen = 0;
				return;
			} /* malformed */
		}

		if (!done) {
			break; /* length field incomplete */
		}

		{
			size_t hdr = 1 + nbytes;
			if ((s_rxlen - off) < (hdr + rl)) {
				break; /* full packet not yet received */
			}
			handle_packet(type, flags, &s_rx[off + hdr], rl);
			off += hdr + rl;
		}
	}

	if (off > 0) {
		memmove(s_rx, s_rx + off, s_rxlen - off);
		s_rxlen -= off;
	}
}

/* --- lwIP TCP callbacks (tcpip thread) ----------------------------------- */

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	LWIP_UNUSED_ARG(arg);

	if ((p == NULL) || (err != ERR_OK)) {
		printf("test-broker: client closed\n");
		if (p != NULL) {
			pbuf_free(p);
		}
		s_conn = NULL;
		tcp_close(pcb);
		return ERR_OK;
	}

	if (s_rxlen + p->tot_len <= sizeof(s_rx)) {
		s_rxlen += pbuf_copy_partial(p, s_rx + s_rxlen, p->tot_len, 0);
		parse_packets();
	}

	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
	LWIP_UNUSED_ARG(arg);
	LWIP_UNUSED_ARG(err);
	s_conn = NULL;
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	LWIP_UNUSED_ARG(arg);
	LWIP_UNUSED_ARG(err);

	printf("test-broker: client connected\n");
	s_conn = newpcb;
	s_rxlen = 0;
	s_dev_id[0] = '\0';
	s_cmd_started = 0;
	tcp_recv(newpcb, on_recv);
	tcp_err(newpcb, on_err);
	return ERR_OK;
}

/* --- command driver ------------------------------------------------------ */

static void cmd_task(void *arg)
{
	static const char *cmds[] = {"status",	"rate 500", "pattern sine",
				     "led on",	"ping",	    "pattern random",
				     "led off", "status"};
	char topic[64];
	LWIP_UNUSED_ARG(arg);

	snprintf(topic, sizeof topic, "dev/%s/cmd", s_dev_id);
	vTaskDelay(pdMS_TO_TICKS(1500)); /* let some telemetry flow first */

	for (size_t i = 0; i < (sizeof(cmds) / sizeof(cmds[0])); i++) {
		printf("test-broker: -> cmd \"%s\"\n", cmds[i]);
		LOCK_TCPIP_CORE();
		send_publish(topic, cmds[i]);
		UNLOCK_TCPIP_CORE();
		vTaskDelay(pdMS_TO_TICKS(1500));
	}

	printf("test-broker: command sequence complete\n");
	vTaskDelete(NULL);
}

static void start_command_sequence(void)
{
	xTaskCreate(cmd_task, "bcmd", configMINIMAL_STACK_SIZE * 4, NULL,
		    tskIDLE_PRIORITY + 2, NULL);
}

void test_broker_start(const struct app_cfg *cfg)
{
	LWIP_UNUSED_ARG(cfg);

	LOCK_TCPIP_CORE();
	s_listen = tcp_new();
	if ((s_listen == NULL) ||
	    (tcp_bind(s_listen, IP_ADDR_ANY, BROKER_PORT) != ERR_OK)) {
		UNLOCK_TCPIP_CORE();
		printf("test-broker: bind failed\n");
		return;
	}
	s_listen = tcp_listen(s_listen);
	tcp_accept(s_listen, on_accept);
	UNLOCK_TCPIP_CORE();

	printf("test-broker: listening on :%d\n", BROKER_PORT);
}
