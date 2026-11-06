#!/usr/bin/env bash
# --frame-trace [--trace-range x|l:HEX:DECIMAL] records original frame boundaries.
set -euo pipefail
cd "$(dirname "$0")/.."
exec tools/run_capped.sh .venv/bin/python tools/oracle.py "$@"
