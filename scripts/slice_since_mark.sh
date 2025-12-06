#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "Usage: $0 MARK_FILE LABEL"
  exit 1
fi

MARK_FILE="$1"
LABEL="$2"

mkdir -p logs

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_COMBINED="logs/slice-${STAMP}-${LABEL}.log"

echo "Creating per-server slices and combined file: $OUT_COMBINED"

echo "# Combined slice for $LABEL at $(date)" > "$OUT_COMBINED"

while read -r line; do
  # skip comment lines
  [[ "$line" =~ ^# ]] && continue

  fname=$(echo "$line" | awk '{print $1}')
  start_line=$(echo "$line" | awk '{print $2}')

  log_path="logs/$fname"

  if [ ! -f "$log_path" ]; then
    echo "Warning: $log_path not found, skipping" >&2
    continue
  fi

  slice_file="logs/slice-${STAMP}-${LABEL}-${fname}"
  # extract lines from (start_line+1) to end
  from=$((start_line + 1))

  # if from is greater than file length, sed will just output nothing; that's fine
  sed -n "${from},\$p" "$log_path" > "$slice_file"

  {
    echo "===== BEGIN $fname ====="
    cat "$slice_file"
    echo "===== END $fname ====="
    echo
  } >> "$OUT_COMBINED"

done < "$MARK_FILE"

# Generate short filtered version
SHORT_FILE="${OUT_COMBINED%.log}-short.log"
grep -Ev "WorkerLoop] No tasks available|Heartbeat] alive|WorkerHeartbeat" \
  "$OUT_COMBINED" > "$SHORT_FILE"
echo "[slice_logs] Short filtered log: $SHORT_FILE"

echo "$OUT_COMBINED"
