#!/bin/sh
# Self-contained MQTT demo without an external broker: one frtos-dev acts as a
# minimal test broker (--run-broker), the other as the device.  They are joined
# by a local UDP-socket link.  The device publishes telemetry; the broker
# drives a command sequence and prints the device's responses.
#
#   scripts/run-mqtt.sh [SECONDS]
#
set -e
BIN=${BIN:-build/frtos-dev}
DURATION=${1:-16}

[ -x "$BIN" ] || { echo "build first: make"; exit 1; }

# Broker (10.0.0.1): binds 20001 -> sends to 20000
"$BIN" --localaddr 127.0.0.1:20001 --udp 127.0.0.1:20000 \
       --ip 10.0.0.1 --mac 02:00:00:00:00:01 --run-broker &
BPID=$!
trap 'kill $BPID 2>/dev/null' EXIT

# Device (10.0.0.2): binds 20000 -> sends to 20001, broker at 10.0.0.1
timeout "$DURATION" \
    "$BIN" --localaddr 127.0.0.1:20000 --udp 127.0.0.1:20001 \
           --ip 10.0.0.2 --mac 02:00:00:00:00:02 --hostname dev01 \
           --broker 10.0.0.1:1883 || true
