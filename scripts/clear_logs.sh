#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$ROOT_DIR/logs"

echo "[clear_logs] Truncating server logs in $LOG_DIR"

mkdir -p "$LOG_DIR"

for f in "$LOG_DIR"/server_*.log; do
  if [ -f "$f" ]; then
    : > "$f"
    echo "  cleared $(basename "$f")"
  fi
done

echo "[clear_logs] Done."
