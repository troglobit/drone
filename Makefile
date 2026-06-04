# Native x86_64 build (gcc + make).  FreeRTOS-Kernel and lwIP each build into
# a static library; the app + port glue is linked against them.  Source
# lists, include dirs, and per-component .a rules live in subdirectory .mk
# fragments included below.

CC       ?= gcc
AR       ?= ar
BUILD    ?= build
BIN       = $(BUILD)/drone
WARN      = -Wall -Wextra
CFLAGS   ?= -O0 -g
INCLUDES  = $(APP_INCS) $(PORT_INCS) $(FREERTOS_INCS) $(LWIP_INCS)
SRCS      = $(APP_SRCS) $(PORT_SRCS) $(FREERTOS_SRCS) $(LWIP_SRCS)
LIBS      = $(BUILD)/liblwip.a $(BUILD)/libfreertos.a
FMT_FILES = $(filter-out port/lwip/lwipopts.h, \
            $(wildcard src/*.[ch] port/*/*.[ch]))
VERSION  := $(shell git describe --always --dirty 2>/dev/null || echo unknown)

# Refresh a tiny stamp file only when VERSION changes, so mqtt_app.o
# rebuilds whenever (and only when) the embedded git-describe string
# moves.  Without this, an incremental `make` after a commit would
# reuse the prior mqtt_app.o and ship a stale fw= string.
$(shell mkdir -p $(BUILD); \
        prev=$$(cat $(BUILD)/.version 2>/dev/null); \
        [ "$$prev" = "$(VERSION)" ] || echo "$(VERSION)" > $(BUILD)/.version)

override CFLAGS  += $(WARN) -pthread -MMD -MP $(INCLUDES) \
                    -DDRONE_VERSION='"$(VERSION)"'
override LDFLAGS += -pthread -lm

include lib/freertos.mk
include lib/lwip.mk
include port/port.mk
include src/app.mk
include test/test.mk

vpath %.c $(sort $(dir $(SRCS)))

all: $(BIN)

$(BIN): $(APP_OBJS) $(PORT_OBJS) $(LIBS) | $(BUILD)
	$(CC) $(APP_OBJS) $(PORT_OBJS) $(LIBS) $(LDFLAGS) -o $@
	@echo "built $@"

$(BUILD)/obj/%.o: %.c | $(BUILD)/obj
	$(CC) $(CFLAGS) -c $< -o $@

# mqtt_app.o embeds DRONE_VERSION; force-rebuild it when the stamp
# (and therefore the git-describe output) has changed since last build.
$(BUILD)/obj/mqtt_app.o: $(BUILD)/.version

$(BUILD) $(BUILD)/obj:
	@mkdir -p $@

run: $(BIN)
	./$(BIN)

fmt:
	@command -v clang-format >/dev/null || { echo "install clang-format"; exit 1; }
	clang-format -i $(FMT_FILES)

clean:
	$(RM) -r $(BUILD)

distclean: clean
	$(RM) tags TAGS GTAGS GRTAGS GPATH GSYMS
	$(RM) core core.* vgcore.*
	find . -name '*~' -not -path './lib/*' -delete

.DEFAULT_GOAL := all
.PHONY: all run fmt clean distclean
