#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
case "${1:-}" in
    title) ;;
    *) echo 'usage: tools/run_port.sh title [port arguments...]' >&2; exit 2 ;;
esac
shift
B=${B:-build/Port}
tools/run_capped.sh make -C port "B=$B"
exec tools/run_capped.sh "port/$B/cybermage" --headless --frames 1 \
    --data "${CM_DATA:-orig/retail}" --dump "${CM_DUMP:-out/port/title}" "$@"
