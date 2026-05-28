# lwIP port glue: a single Ethernet netif riding on a host UDP socket,
# using QEMU's "-netdev socket,udp=" framing.  Included by the top Makefile.
#
# Exports:
#   PORT_SRCS, PORT_INCS, PORT_OBJS

PORT_SRCS = port/lwip/qeneth_netif.c

# port/lwip/ also hosts lwipopts.h and arch/cc.h, found via LWIP_INCS.
PORT_INCS =

PORT_OBJS = $(addprefix $(BUILD)/obj/,$(notdir $(PORT_SRCS:.c=.o)))

-include $(PORT_OBJS:.o=.d)
