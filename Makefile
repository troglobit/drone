# drone - FreeRTOS + lwIP MQTT end-device for the qeneth test lab.
#
# Native x86_64 build (gcc + make): FreeRTOS-Kernel POSIX port + lwIP, attached
# to a qeneth UDP-socket link via a custom lwIP netif.

CC      ?= gcc
BUILD   ?= build
BIN      = $(BUILD)/drone

KERNEL   = lib/FreeRTOS-Kernel
PORT     = $(KERNEL)/portable/ThirdParty/GCC/Posix
LWIPDIR  = lib/lwip/src
LWIPCONTRIB = lib/lwip/contrib

# --- FreeRTOS kernel ---------------------------------------------------------
KERNEL_SRCS = \
	$(KERNEL)/tasks.c \
	$(KERNEL)/queue.c \
	$(KERNEL)/list.c \
	$(KERNEL)/timers.c \
	$(KERNEL)/event_groups.c \
	$(KERNEL)/stream_buffer.c \
	$(KERNEL)/portable/MemMang/heap_4.c \
	$(PORT)/port.c \
	$(PORT)/utils/wait_for_event.c

# --- lwIP (IPv4 core + ethernet + tcpip thread) ------------------------------
LWIP_CORE = $(addprefix $(LWIPDIR)/core/, \
	init.c def.c dns.c inet_chksum.c ip.c mem.c memp.c netif.c pbuf.c raw.c \
	stats.c sys.c altcp.c altcp_alloc.c altcp_tcp.c tcp.c tcp_in.c tcp_out.c \
	timeouts.c udp.c)
LWIP_CORE4 = $(addprefix $(LWIPDIR)/core/ipv4/, \
	acd.c autoip.c dhcp.c etharp.c icmp.c igmp.c ip4_frag.c ip4.c ip4_addr.c)
LWIP_SRCS = \
	$(LWIP_CORE) \
	$(LWIP_CORE4) \
	$(LWIPDIR)/netif/ethernet.c \
	$(LWIPDIR)/api/tcpip.c \
	$(LWIPDIR)/api/err.c \
	$(LWIPDIR)/apps/mqtt/mqtt.c \
	$(LWIPCONTRIB)/ports/freertos/sys_arch.c

# --- Application + port glue -------------------------------------------------
APP_SRCS = \
	src/main.c \
	src/net.c \
	src/ping.c \
	src/mqtt_app.c \
	src/test_broker.c \
	port/lwip/qeneth_netif.c

SRCS = $(APP_SRCS) $(KERNEL_SRCS) $(LWIP_SRCS)

INCS = \
	-Iinclude \
	-Iport/lwip \
	-I$(KERNEL)/include \
	-I$(PORT) \
	-I$(PORT)/utils \
	-I$(LWIPDIR)/include \
	-I$(LWIPCONTRIB)/ports/freertos/include

# --- Flags -------------------------------------------------------------------
WARN     = -Wall -Wextra
CFLAGS  ?= -O0 -g
CFLAGS  += $(WARN) -pthread -MMD -MP $(INCS)
LDFLAGS += -pthread -lm

# Flat objects keyed by basename (all sources have unique basenames); vpath
# locates each source in its directory.
OBJS = $(addprefix $(BUILD)/obj/,$(notdir $(SRCS:.c=.o)))
vpath %.c $(sort $(dir $(SRCS)))

# --- Rules -------------------------------------------------------------------
.PHONY: all run test clean
all: $(BIN)

$(BIN): $(OBJS) | $(BUILD)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo "built $@"

$(BUILD)/obj/%.o: %.c | $(BUILD)/obj
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD) $(BUILD)/obj:
	@mkdir -p $@

-include $(OBJS:.o=.d)

run: $(BIN)
	./$(BIN)

# Dependency-free self-tests: two back-to-back drone processes over a local
# UDP-socket link (no broker, no root, no qeneth).
test: $(BIN)
	@echo "== ping self-test =="
	@utils/run-pair.sh 3
	@echo "== mqtt self-test =="
	@utils/run-mqtt.sh 12

clean:
	$(RM) -r $(BUILD)
