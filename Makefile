# frtos-dev - FreeRTOS+lwIP MQTT end-device for the qeneth test lab.
#
# Builds the FreeRTOS-Kernel POSIX/Linux simulator port as a native x86_64
# binary with plain gcc + make.  Later milestones add lwIP and MQTT sources.

CC      ?= gcc
BUILD   ?= build
BIN      = $(BUILD)/frtos-dev

KERNEL   = lib/FreeRTOS-Kernel
PORT     = $(KERNEL)/portable/ThirdParty/GCC/Posix

# --- Sources -----------------------------------------------------------------
SRCS = \
	src/main.c \
	$(KERNEL)/tasks.c \
	$(KERNEL)/queue.c \
	$(KERNEL)/list.c \
	$(KERNEL)/timers.c \
	$(KERNEL)/event_groups.c \
	$(KERNEL)/stream_buffer.c \
	$(KERNEL)/portable/MemMang/heap_4.c \
	$(PORT)/port.c \
	$(PORT)/utils/wait_for_event.c

INCS = \
	-Iinclude \
	-I$(KERNEL)/include \
	-I$(PORT) \
	-I$(PORT)/utils

# --- Flags -------------------------------------------------------------------
WARN     = -Wall -Wextra
CFLAGS  ?= -O0 -g
CFLAGS  += $(WARN) -pthread $(INCS)
LDFLAGS += -pthread

# Objects are flat in $(BUILD)/obj keyed by basename (all unique); vpath finds
# the matching source in its directory.
OBJS = $(addprefix $(BUILD)/obj/,$(notdir $(SRCS:.c=.o)))
vpath %.c $(sort $(dir $(SRCS)))

# --- Rules -------------------------------------------------------------------
.PHONY: all run clean
all: $(BIN)

$(BIN): $(OBJS) | $(BUILD)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo "built $@"

$(BUILD)/obj/%.o: %.c | $(BUILD)/obj
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD) $(BUILD)/obj:
	@mkdir -p $@

run: $(BIN)
	./$(BIN)

clean:
	$(RM) -r $(BUILD)
