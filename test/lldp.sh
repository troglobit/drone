#!/bin/sh
# LLDP-driven broker discovery: device starts with NO --broker; the peer
# drone (--run-broker) advertises its mgmt IP via LLDP; device picks it up,
# connects, and a command round-trips end-to-end.
#
#   test/lldp.sh [SECONDS]
#
set -e
. "$(cd "$(dirname "$0")" && pwd)/../utils/drone.sh"
drone_require_bin

DURATION=${1:-12}

LOG=${LOG:-/tmp/drone-lldp-bg.log}
DEVLOG=${DEVLOG:-/tmp/drone-lldp-dev.log}

# Broker drone (10.0.0.1): binds 20001 -> sends to 20000.  Fast LLDP so the
# device sees the first frame within a second.
drone_run_bg --localaddr 127.0.0.1:20001 --udp 127.0.0.1:20000 \
             --ip 10.0.0.1 --mac 02:00:00:00:00:01 \
             --lldp-interval 1 --run-broker

# Device drone: NO --broker; relies on LLDP discovery.
timeout "$DURATION" \
    "$BIN" --localaddr 127.0.0.1:20000 --udp 127.0.0.1:20001 \
           --ip 10.0.0.2 --mac 02:00:00:00:00:02 --hostname dev01 \
           --lldp-interval 1 > "$DEVLOG" 2>&1 || true

ok=1
if ! grep -q 'lldp: neighbor mgmt addr 10.0.0.1' "$DEVLOG"; then
    echo "FAIL: drone didn't observe the LLDP-advertised broker IP"
    ok=0
fi
if ! grep -q 'mqtt: connecting to 10.0.0.1:1883' "$DEVLOG"; then
    echo "FAIL: drone didn't connect to the LLDP-discovered broker"
    ok=0
fi
if ! grep -qE 'test-broker: <- \[dev/dev01/resp\]' "$LOG"; then
    echo "FAIL: no MQTT command response observed"
    ok=0
fi

if [ "$ok" -eq 1 ]; then
    n=$(grep -cE 'test-broker: <- \[dev/dev01/resp\]' "$LOG")
    echo "PASS: lldp discovery + broker connect ($n response(s) round-tripped)"
else
    echo "--- broker ---"; cat "$LOG"
    echo "--- device ---"; cat "$DEVLOG"
    exit 1
fi
