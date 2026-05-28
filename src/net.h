/*
 * Application/network configuration and bring-up for drone.
 */
#ifndef NET_H
#define NET_H

#include <stdint.h>

struct app_cfg {
	char localaddr[64]; /* "host:port" to bind the link socket to    */
	char peeraddr[64];  /* "host:port" datagrams are sent to         */
	char ip[16];	    /* static IPv4 address                       */
	char netmask[16];
	char gw[16];
	uint8_t mac[6];
	char hostname[32]; /* also used as the MQTT client/device id     */

	char broker_ip[16]; /* MQTT broker address                        */
	int broker_port;
	int is_broker; /* run the built-in test broker fixture       */
	int no_mqtt;   /* skip the MQTT client (diagnostics only)    */

	int do_ping; /* run the built-in pinger after bring-up    */
	char ping_target[16];
	int ping_count;
};

void app_cfg_defaults(struct app_cfg *c);
int app_cfg_parse(struct app_cfg *c, int argc, char **argv); /* 0 = ok */
void app_cfg_finalize(struct app_cfg *c); /* derive defaults that depend
						 on other fields (hostname). */

/* Initialise lwIP, bring up the interface, and start enabled services.
 * Must be called from a FreeRTOS task (after the scheduler is running). */
void net_start(const struct app_cfg *c);

#endif /* NET_H */
