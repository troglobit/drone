#!/bin/sh
# Back-to-back self-test without qeneth: two drone instances joined by a
# local UDP-socket link (the same framing qeneth uses).  The "responder"
# (10.0.0.1) answers ARP/ICMP; the "pinger" (10.0.0.2) sends COUNT echoes.
#
#   test/pair.sh [COUNT]
#
set -e
. "$(cd "$(dirname "$0")" && pwd)/../utils/drone.sh"
drone_require_bin

COUNT=${1:-3}

LOG=${LOG:-/tmp/drone-pair-bg.log}
PINGLOG=${PINGLOG:-/tmp/drone-pair-pinger.log}

drone_run_bg --localaddr 127.0.0.1:20001 --udp 127.0.0.1:20000 \
             --ip 10.0.0.1 --mac 02:00:00:00:00:01 --hostname responder --no-mqtt

# The drone runs forever; bound the pinger so the self-test returns.
timeout "$((COUNT + 4))" \
    "$BIN" --localaddr 127.0.0.1:20000 --udp 127.0.0.1:20001 \
           --ip 10.0.0.2 --mac 02:00:00:00:00:02 --hostname pinger \
           --no-mqtt --ping 10.0.0.1 "$COUNT" > "$PINGLOG" 2>&1 || true

if grep -qE "ping: $COUNT sent, $COUNT received, 0% loss" "$PINGLOG"; then
    echo "PASS: pair $COUNT/$COUNT echoes round-tripped"
else
    echo "FAIL: ping did not round-trip"
    echo "--- pinger ---"; cat "$PINGLOG"
    echo "--- responder ---"; cat "$LOG"
    exit 1
fi
