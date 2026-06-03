#!/bin/sh
# LLDP-gated DHCP start.  The device drone runs with --dhcp and no --ip;
# DHCP is not attempted until the peer's LLDP frame arrives.  No real
# DHCP server is in the fixture, so after dhcp_start() lwIP's
# cooperative AutoIP (LWIP_DHCP_AUTOIP_COOP) falls back to a 169.254/16
# claim once DHCP exhausts its early retries.  We assert that the gating
# message fires and that AutoIP eventually wins.
#
#   test/dhcp-lldp.sh [SECONDS]
#
set -e
. "$(cd "$(dirname "$0")" && pwd)/../utils/drone.sh"
drone_require_bin

# AutoIP fallback timing in a no-server fixture: ~1s for LLDP, then
# dhcp_start; ~2-3s for two DISCOVER retries to trigger LWIP_DHCP_
# AUTOIP_COOP; then AutoIP probe-and-announce takes a few seconds more.
# 22s gives generous slack.
DURATION=${1:-22}

LOG=${LOG:-/tmp/drone-dhcp-lldp-bg.log}
DEVLOG=${DEVLOG:-/tmp/drone-dhcp-lldp-dev.log}

# Peer drone (10.0.0.1): static IP, fast LLDP so the device sees a frame
# within a second.
drone_run_bg --localaddr 127.0.0.1:40001 --udp 127.0.0.1:40000 \
             --ip 10.0.0.1 --mac 02:00:00:00:00:01 \
             --hostname peer --no-mqtt --lldp --lldp-interval 1

# Device drone: --dhcp + --lldp, NO --ip.  Should wait for LLDP, then
# start DHCP, then fall back to AutoIP after lwIP's COOP_TRIES exhausts.
timeout "$DURATION" \
    "$BIN" --localaddr 127.0.0.1:40000 --udp 127.0.0.1:40001 \
           --mac 02:00:00:00:00:02 --hostname dhcp-test \
           --dhcp --lldp --lldp-interval 1 --no-mqtt > "$DEVLOG" 2>&1 || true

ok=1
if ! grep -q 'interface up (DHCP, gated on LLDP neighbor)' "$DEVLOG"; then
    echo "FAIL: device didn't enter LLDP-gated mode"
    ok=0
fi
if ! grep -q 'LLDP neighbor seen, starting DHCP' "$DEVLOG"; then
    echo "FAIL: gating trigger didn't fire after LLDP arrival"
    ok=0
fi
if ! grep -qE 'drone: ipv4 address: 169\.254\.' "$DEVLOG"; then
    echo "FAIL: AutoIP fallback didn't claim a link-local address"
    ok=0
fi

if [ "$ok" -eq 1 ]; then
    echo "PASS: dhcp-lldp gating + AutoIP fallback"
else
    echo "--- peer ---"; cat "$LOG"
    echo "--- device ---"; cat "$DEVLOG"
    exit 1
fi
