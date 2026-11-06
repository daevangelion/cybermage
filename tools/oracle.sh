#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
exec tools/run_capped.sh .venv/bin/python tools/oracle.py "$@"
