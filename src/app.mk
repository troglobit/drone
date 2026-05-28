# Application sources.  Included by the top Makefile.
#
# Exports:
#   APP_SRCS, APP_INCS, APP_OBJS

APP_SRCS = \
	src/main.c \
	src/net.c \
	src/ping.c \
	src/mqtt_app.c \
	src/mdns_app.c \
	src/test_broker.c

# App headers (mdns_app.h, mqtt_app.h, ...) sit alongside their sources, so
# gcc's local-include search finds them via "..." -- no -Isrc needed.
APP_INCS =

APP_OBJS = $(addprefix $(BUILD)/obj/,$(notdir $(APP_SRCS:.c=.o)))

-include $(APP_OBJS:.o=.d)
