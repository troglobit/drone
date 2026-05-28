# lwIP build fragment.  Included by the top Makefile.
#
# Builds the lwIP core (IPv4 + IPv6), the ethernet netif, the tcpip thread,
# the FreeRTOS sys_arch port, and the mqtt + mdns apps into a single
# static library.
#
# Exports:
#   LWIP_SRCS, LWIP_INCS, LWIP_OBJS
#   Rule that builds $(BUILD)/liblwip.a

LWIP_DIR     = lib/lwip/src
LWIP_CONTRIB = lib/lwip/contrib

LWIP_CORE = $(addprefix $(LWIP_DIR)/core/, \
	init.c def.c dns.c inet_chksum.c ip.c mem.c memp.c netif.c pbuf.c raw.c \
	stats.c sys.c altcp.c altcp_alloc.c altcp_tcp.c tcp.c tcp_in.c tcp_out.c \
	timeouts.c udp.c)
LWIP_CORE4 = $(addprefix $(LWIP_DIR)/core/ipv4/, \
	acd.c autoip.c dhcp.c etharp.c icmp.c igmp.c ip4_frag.c ip4.c ip4_addr.c)
LWIP_CORE6 = $(addprefix $(LWIP_DIR)/core/ipv6/, \
	dhcp6.c ethip6.c icmp6.c inet6.c ip6.c ip6_addr.c ip6_frag.c mld6.c nd6.c)

LWIP_SRCS = \
	$(LWIP_CORE) \
	$(LWIP_CORE4) \
	$(LWIP_CORE6) \
	$(LWIP_DIR)/netif/ethernet.c \
	$(LWIP_DIR)/api/tcpip.c \
	$(LWIP_DIR)/api/err.c \
	$(LWIP_DIR)/apps/mqtt/mqtt.c \
	$(LWIP_DIR)/apps/mdns/mdns.c \
	$(LWIP_DIR)/apps/mdns/mdns_domain.c \
	$(LWIP_DIR)/apps/mdns/mdns_out.c \
	$(LWIP_CONTRIB)/ports/freertos/sys_arch.c

LWIP_INCS = \
	-Iport/lwip \
	-I$(LWIP_DIR)/include \
	-I$(LWIP_CONTRIB)/ports/freertos/include

LWIP_OBJS = $(addprefix $(BUILD)/obj/,$(notdir $(LWIP_SRCS:.c=.o)))

$(BUILD)/liblwip.a: $(LWIP_OBJS) | $(BUILD)
	$(RM) $@
	$(AR) rcs $@ $^

-include $(LWIP_OBJS:.o=.d)
