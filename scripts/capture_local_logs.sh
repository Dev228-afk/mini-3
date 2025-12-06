#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$ROOT_DIR/logs"
mkdir -p "$LOG_DIR"

LABEL="local"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --label)
      LABEL="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

TIMESTAMP="$(date +"%Y%m%d-%H%M%S")"
OUTPUT="$LOG_DIR/run-${TIMESTAMP}-${LABEL}.log"

echo "Bundling logs for label='$LABEL' into $OUTPUT"

{
  for f in "$LOG_DIR"/server_*.log; do
    [ -f "$f" ] || continue
    echo "===== BEGIN $(basename "$f") ====="
    cat "$f"
    echo "===== END $(basename "$f") ====="
    echo
  done
} > "$OUTPUT"

echo "Done. Combined log: $OUTPUT"
