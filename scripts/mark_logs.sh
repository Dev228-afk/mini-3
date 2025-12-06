#!/usr/bin/env bash
set -euo pipefail

mkdir -p logs

# Label for this mark (optional, but nice)
LABEL="${1:-mark}"

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="logs/mark-${LABEL}-${STAMP}.txt"

echo "# log mark created at $(date)" > "$OUT"

for f in logs/server_*.log; do
  if [ -f "$f" ]; then
    # just the line count, no filename
    count=$(wc -l < "$f")
    base=$(basename "$f")
    echo "$base $count" >> "$OUT"
  fi
done

echo "$OUT"
