#!/usr/bin/env bash
# run_capped.sh CMD...: run one heavy job (emulator, vector generator, probe,
# full build/test) under a memory cap and a host-wide slot limit.
# Slots: ASP_SLOTS (default 10) concurrent jobs across all agents; a job waits
# for a free slot. Memory: ASP_MEM (default 2500M) per job, no swap; a job that
# exceeds it is killed instead of pushing the host into swap/OOM.
set -euo pipefail
[[ $# -ge 1 ]] || { echo "usage: tools/run_capped.sh CMD..." >&2; exit 2; }
SLOTS="${ASP_SLOTS:-10}"
MEM="${ASP_MEM:-2500M}"
LOCKDIR="${XDG_RUNTIME_DIR:-/tmp}/aspetra-slots"
mkdir -p "$LOCKDIR"
while :; do
	for ((i = 0; i < SLOTS; i++)); do
		exec {fd}>"$LOCKDIR/slot$i"
		if flock -n "$fd"; then
			# systemd-run closes inherited fds, so this shell keeps the lock
			# until the job exits.
			rc=0
			systemd-run --user --scope -q -p MemoryMax="$MEM" \
				-p MemorySwapMax=0 -- "$@" {fd}>&- || rc=$?
			exit "$rc"
		fi
		exec {fd}>&-
	done
	sleep 2
done
