#!/bin/sh
# Back-to-back self-test without qeneth: two drone instances joined by a
# local UDP-socket link (the same framing qeneth uses).  The "responder"
# (10.0.0.1) answers ARP/ICMP; the "pinger" (10.0.0.2) sends COUNT echoes.
#
#   scripts/run-pair.sh [COUNT]
#
set -e
BIN=${BIN:-build/drone}
COUNT=${1:-3}

[ -x "$BIN" ] || { echo "build first: make"; exit 1; }

"$BIN" --localaddr 127.0.0.1:20001 --udp 127.0.0.1:20000 \
       --ip 10.0.0.1 --mac 02:00:00:00:00:01 --hostname responder --no-mqtt &
RPID=$!
trap 'kill $RPID 2>/dev/null' EXIT

# The device runs forever; bound the pinger so the self-test returns.
timeout "$((COUNT + 4))" \
    "$BIN" --localaddr 127.0.0.1:20000 --udp 127.0.0.1:20001 \
           --ip 10.0.0.2 --mac 02:00:00:00:00:02 --hostname pinger \
           --no-mqtt --ping 10.0.0.1 "$COUNT" || true
