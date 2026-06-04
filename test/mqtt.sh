#!/bin/sh
# Self-contained MQTT demo without an external broker: one drone acts as a
# minimal test broker (--run-broker), the other as the device.  They are
# joined by a local UDP-socket link.  The device publishes telemetry; the
# broker drives a command sequence and prints the device's responses.
#
#   test/mqtt.sh [SECONDS]
#
set -e
. "$(cd "$(dirname "$0")" && pwd)/../utils/drone.sh"
drone_require_bin

DURATION=${1:-16}

# Broker (10.0.0.1): binds 20001 -> sends to 20000
LOG=${LOG:-/tmp/drone-mqtt-bg.log}
DEVLOG=${DEVLOG:-/tmp/drone-mqtt-dev.log}

drone_run_bg --localaddr 127.0.0.1:20001 --udp 127.0.0.1:20000 \
             --ip 10.0.0.1 --mac 02:00:00:00:00:01 --run-broker

# Device (10.0.0.2): binds 20000 -> sends to 20001, broker at 10.0.0.1
timeout "$DURATION" \
    "$BIN" --localaddr 127.0.0.1:20000 --udp 127.0.0.1:20001 \
           --ip 10.0.0.2 --mac 02:00:00:00:00:02 --hostname dev01 \
           --broker 10.0.0.1:1883 > "$DEVLOG" 2>&1 || true

ok=1

# Command round-trip: at least one response on dev/dev01/resp.
if ! grep -qE 'test-broker: <- \[dev/dev01/resp\]' "$LOG"; then
    echo "FAIL: no MQTT command response observed"
    ok=0
fi

# Telemetry payload must carry the new temp + uptime fields.
if ! grep -qE 'test-broker: <- \[dev/dev01/telemetry\].*"temp":' "$LOG"; then
    echo "FAIL: telemetry payload missing temp"
    ok=0
fi
if ! grep -qE 'test-broker: <- \[dev/dev01/telemetry\].*"uptime":' "$LOG"; then
    echo "FAIL: telemetry payload missing uptime"
    ok=0
fi

# Retained identity publish on dev/dev01/info at CONNACK.
if ! grep -qE 'test-broker: <- \[dev/dev01/info\].*"mfg":"Drone Sim".*"model":"drone".*"fw":' "$LOG"; then
    echo "FAIL: missing retained dev/dev01/info publish"
    ok=0
fi

if [ "$ok" -eq 1 ]; then
    n=$(grep -cE 'test-broker: <- \[dev/dev01/resp\]' "$LOG")
    echo "PASS: mqtt $n response(s) + telemetry temp/uptime + retained info"
else
    echo "--- broker ---"; cat "$LOG"
    echo "--- device ---"; cat "$DEVLOG"
    exit 1
fi
