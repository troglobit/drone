#!/bin/sh
# Verify the mDNS responder: a drone with a hostname registers
# <hostname>.local and announces a _drone._tcp service.  This only checks
# the local registration log; a peer-side query exercise needs two drones
# (a future utility).
set -e
. "$(cd "$(dirname "$0")" && pwd)/../utils/drone.sh"
drone_require_bin

LOG=${LOG:-/tmp/drone-mdns.log}

timeout 4 "$BIN" --localaddr 127.0.0.1:20000 --udp 127.0.0.1:20001 \
    --hostname north-pump --ip 10.0.0.2 --no-mqtt > "$LOG" 2>&1 || true

if grep -qE 'mdns: announcing north-pump\.local [+] _drone\._tcp' "$LOG"; then
    echo "PASS: mDNS responder registered north-pump.local + _drone._tcp"
else
    echo "FAIL: mDNS responder did not announce"
    echo "--- log ---"
    cat "$LOG"
    exit 1
fi
