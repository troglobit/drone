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

$(BUILD)/obj/%.o: %.c | $(BUILD)/obj
	$(CC) $(CFLAGS) -c $< -o $@

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
