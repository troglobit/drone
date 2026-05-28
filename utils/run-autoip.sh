#!/bin/sh
# Verify IPv4 AutoIP: a drone started with no --ip claims a 169.254.x.x
# address via RFC 3927 (probe + announce).  No peer is needed - lwIP's
# AutoIP claims once the probe interval elapses with no conflict.
set -e
BIN=${BIN:-build/drone}
LOG=${LOG:-/tmp/drone-autoip.log}
[ -x "$BIN" ] || { echo "build first: make"; exit 1; }

timeout 16 "$BIN" --localaddr 127.0.0.1:20000 --udp 127.0.0.1:20001 \
    --mac 02:00:00:00:00:02 --no-mqtt > "$LOG" 2>&1 || true

if grep -qE 'ipv4 address: 169\.254\.' "$LOG"; then
    addr=$(grep -oE '169\.254\.[0-9]+\.[0-9]+' "$LOG" | head -1)
    echo "PASS: AutoIP claimed $addr"
else
    echo "FAIL: no AutoIP claim observed"
    echo "--- log ---"
    cat "$LOG"
    exit 1
fi
