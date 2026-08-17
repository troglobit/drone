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
INCLUDES  = -I$(BUILD) $(APP_INCS) $(PORT_INCS) $(FREERTOS_INCS) $(LWIP_INCS)
SRCS      = $(APP_SRCS) $(PORT_SRCS) $(FREERTOS_SRCS) $(LWIP_SRCS)
LIBS      = $(BUILD)/liblwip.a $(BUILD)/libfreertos.a
FMT_FILES = $(filter-out port/lwip/lwipopts.h, \
            $(wildcard src/*.[ch] port/*/*.[ch]))
VERSION  := $(shell git describe --always --dirty 2>/dev/null || echo unknown)

override CFLAGS  += $(WARN) -pthread -MMD -MP $(INCLUDES)
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

$(BUILD)/obj/%.o: %.c | $(BUILD)/obj $(BUILD)/version.h
	$(CC) $(CFLAGS) -c $< -o $@

# The version reaches the objects through a generated header rather than a -D
# so that -MMD dependency tracking sees it move: an incremental `make` after a
# commit would otherwise reuse objects built against the old string and ship a
# stale fw= in the telemetry.  FORCE re-checks it on every build, but the file
# is rewritten only when git describe output differs, so the .d files rebuild
# exactly the objects that include it -- and nothing else.
$(BUILD)/version.h: FORCE | $(BUILD)
	@new='#define DRONE_VERSION "$(VERSION)"'; \
	 [ "$$new" = "$$(cat $@ 2>/dev/null)" ] || echo "$$new" > $@

FORCE:

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
