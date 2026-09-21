#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
DRAXUL_EXE="$(python3 "$SCRIPT_DIR/draxul_paths.py" --root "$ROOT")"

if [[ "${1:-}" == "--print-executable" ]]; then
    printf '%s\n' "$DRAXUL_EXE"
    exit 0
fi

LOG_FILE="${1:-$ROOT/logs/mac-log.txt}"

if [[ ! -x "$DRAXUL_EXE" ]]; then
    echo "Built app not found or not executable: $DRAXUL_EXE" >&2
    echo "Build it first with: python3 do.py build debug" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG_FILE")"
echo "Running draxul, logging to $LOG_FILE ..."
"$DRAXUL_EXE" >"$LOG_FILE" 2>&1
echo "Done. Log saved to $LOG_FILE"
