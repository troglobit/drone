# FreeRTOS-Kernel build fragment.  Included by the top Makefile.
#
# Exports:
#   FREERTOS_SRCS, FREERTOS_INCS, FREERTOS_OBJS
#   Rule that builds $(BUILD)/libfreertos.a

FREERTOS_DIR  = lib/FreeRTOS-Kernel
FREERTOS_PORT = $(FREERTOS_DIR)/portable/ThirdParty/GCC/Posix

FREERTOS_SRCS = \
	$(FREERTOS_DIR)/tasks.c \
	$(FREERTOS_DIR)/queue.c \
	$(FREERTOS_DIR)/list.c \
	$(FREERTOS_DIR)/timers.c \
	$(FREERTOS_DIR)/event_groups.c \
	$(FREERTOS_DIR)/stream_buffer.c \
	$(FREERTOS_DIR)/portable/MemMang/heap_4.c \
	$(FREERTOS_PORT)/port.c \
	$(FREERTOS_PORT)/utils/wait_for_event.c

FREERTOS_INCS = \
	-Iinclude \
	-I$(FREERTOS_DIR)/include \
	-I$(FREERTOS_PORT) \
	-I$(FREERTOS_PORT)/utils

FREERTOS_OBJS = $(addprefix $(BUILD)/obj/,$(notdir $(FREERTOS_SRCS:.c=.o)))

$(BUILD)/libfreertos.a: $(FREERTOS_OBJS) | $(BUILD)
	$(RM) $@
	$(AR) rcs $@ $^

-include $(FREERTOS_OBJS:.o=.d)
