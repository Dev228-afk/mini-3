#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

LOG_DIR="$ROOT_DIR/logs"
mkdir -p "$LOG_DIR"

TIMESTAMP="$(date +"%Y%m%d-%H%M%S")"
RUN_DIR="$LOG_DIR/run-$TIMESTAMP"
mkdir -p "$RUN_DIR"

echo "=== Mini-3 capture_run at $TIMESTAMP ==="

# Move existing server logs into the run directory so we have a clean slate
find "$LOG_DIR" -maxdepth 1 -type f -name "server_*.log" -exec mv {} "$RUN_DIR"/ \; || true

# Run the real data test (this will talk to existing servers)
"$SCRIPT_DIR/test_real_data.sh" --dataset test_data/data_100k.csv

echo "Client finished, bundling logs..."

# Combine latest server logs into a single file
OUTPUT="$LOG_DIR/run-$TIMESTAMP-combined.log"
{
  for f in "$LOG_DIR"/server_*.log; do
    [ -f "$f" ] || continue
    echo "===== BEGIN $f ====="
    cat "$f"
    echo "===== END $f ====="
    echo
  done
} > "$OUTPUT"

echo "Combined logs written to $OUTPUT"
