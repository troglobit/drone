#!/bin/sh
# Pass `--broker broker1.local:1883` and let drone resolve the hostname via
# mDNS (the peer drone runs lwIP's responder for "broker1.local").  Verifies
# the new mDNS querier path end-to-end: build + send query, parse answer,
# hand the IPv4 to mqtt_app, connect, round-trip a command.
#
#   test/mdns-broker.sh [SECONDS]
#
set -e
. "$(cd "$(dirname "$0")" && pwd)/../utils/drone.sh"
drone_require_bin

DURATION=${1:-12}

LOG=${LOG:-/tmp/drone-mdns-broker-bg.log}
DEVLOG=${DEVLOG:-/tmp/drone-mdns-broker-dev.log}

# Broker drone: hostname "broker1" -> announces "broker1.local" via lwIP mDNS.
drone_run_bg --localaddr 127.0.0.1:28001 --udp 127.0.0.1:28000 \
             --ip 10.0.0.1 --mac 02:00:00:00:00:01 \
             --hostname broker1 --run-broker

# Device drone: --broker broker1.local -> mDNS query -> 10.0.0.1 -> MQTT.
timeout "$DURATION" \
    "$BIN" --localaddr 127.0.0.1:28000 --udp 127.0.0.1:28001 \
           --ip 10.0.0.2 --mac 02:00:00:00:00:02 --hostname dev01 \
           --broker broker1.local:1883 > "$DEVLOG" 2>&1 || true

ok=1
if ! grep -qE 'mqtt: connecting to 10\.0\.0\.1:1883' "$DEVLOG"; then
    echo "FAIL: drone didn't connect to the mDNS-resolved broker"
    ok=0
fi
if ! grep -qE 'test-broker: <- \[dev/dev01/resp\]' "$LOG"; then
    echo "FAIL: no MQTT command response observed"
    ok=0
fi

if [ "$ok" -eq 1 ]; then
    n=$(grep -cE 'test-broker: <- \[dev/dev01/resp\]' "$LOG")
    echo "PASS: mdns resolved broker1.local; broker round-trip ($n response(s))"
else
    echo "--- broker ---"; cat "$LOG"
    echo "--- device ---"; cat "$DEVLOG"
    exit 1
fi
