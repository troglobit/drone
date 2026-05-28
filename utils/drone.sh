#!/bin/sh
# Shared helpers for shell scripts that drive the drone binary.
#
# Source from a sibling script with:
#
#       . "$(cd "$(dirname "$0")" && pwd)/../utils/drone.sh"
#
# Honours these environment variables (override-friendly):
#       BIN   path to the drone binary (default: build/drone, relative to
#             the current working directory)
#       LOG   path used by drone_run_bg for redirected output

: "${BIN:=build/drone}"

# Bail out with a clear message if the binary hasn't been built.
drone_require_bin()
{
	[ -x "$BIN" ] || { echo "build first: make"; exit 1; }
}

# Start a drone in the background with the supplied args, redirecting
# output to "$LOG" (caller-set; we just use whatever is in the env).  The
# pid lands in DRONE_BG_PID and we install an EXIT trap to kill it so the
# caller can exit cleanly without leaking the child.
drone_run_bg()
{
	"$BIN" "$@" > "${LOG:?LOG not set}" 2>&1 &
	DRONE_BG_PID=$!
	trap 'kill "$DRONE_BG_PID" 2>/dev/null' EXIT
}
