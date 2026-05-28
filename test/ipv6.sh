#!/bin/sh
# Verify IPv6 link-local: a drone brings up a fe80::EUI64/10 address on
# every netif at link-up.  The address is derived from the MAC and
# announced via DAD; the netif ext-callback logs it once it becomes valid.
set -e
. "$(cd "$(dirname "$0")" && pwd)/../utils/drone.sh"
drone_require_bin

LOG=${LOG:-/tmp/drone-ipv6.log}

timeout 6 "$BIN" --localaddr 127.0.0.1:20000 --udp 127.0.0.1:20001 \
    --mac 02:00:00:00:00:02 --ip 10.0.0.2 --no-mqtt > "$LOG" 2>&1 || true

if grep -qiE 'ipv6 address: fe80:' "$LOG"; then
    addr=$(grep -oiE 'fe80:[0-9a-f:]+' "$LOG" | head -1)
    echo "PASS: link-local v6 ready: $addr"
else
    echo "FAIL: no fe80:: address observed"
    echo "--- log ---"
    cat "$LOG"
    exit 1
fi
