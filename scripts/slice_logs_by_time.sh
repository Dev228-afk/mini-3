#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "Usage: $0 \"START\" \"END\" [label]" >&2
  echo "Example: $0 \"2025-12-05 23:34:40\" \"2025-12-05 23:34:47\" data_1m" >&2
  exit 1
fi

START="$1"        # "YYYY-MM-DD HH:MM:SS"
END="$2"          # "YYYY-MM-DD HH:MM:SS"
LABEL="${3:-window}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$ROOT_DIR/logs"

TS="$(date +%Y%m%d-%H%M%S)"
OUT_FILE="$LOG_DIR/slice-${TS}-${LABEL}.log"

echo "[slice_logs] START=$START END=$END LABEL=$LABEL"
echo "[slice_logs] writing to $OUT_FILE"

{
  for f in "$LOG_DIR"/server_*.log; do
    [ -f "$f" ] || continue
    base="$(basename "$f")"
    echo "===== BEGIN $base ====="
    awk -v s="$START" -v e="$END" '
      NF >= 2 {
        ts = $1 " " $2
        if (ts >= s && ts <= e) {
          print
        }
      }
    ' "$f"
    echo "===== END $base ====="
    echo
  done
} > "$OUT_FILE"

echo "[slice_logs] done."
echo "[slice_logs] Output: $OUT_FILE"
