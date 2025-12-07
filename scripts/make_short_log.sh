#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <combined-slice.log> [output-file]" >&2
  exit 1
fi

INPUT="$1"

if [[ ! -f "$INPUT" ]]; then
  echo "Error: Input file not found: $INPUT" >&2
  exit 1
fi

if [[ $# -ge 2 ]]; then
  OUTPUT="$2"
else
  # Default: strip .log and append -short.log
  BASE="${INPUT%.log}"
  OUTPUT="${BASE}-short.log"
fi

awk '
# Always keep structure markers
/^=====/ { print; next }
/^#/     { print; next }

# DROP noise aggressively
/Heartbeat/              { next }
/No tasks available/     { next }
/NodeControl/            { next }
/RequestTask from/       { next }
/RequestTaskForWorker/   { next }
/Updated stats/          { next }
/^[[:space:]]*$/         { next }

# KEEP interesting metrics-friendly lines
/Worker] Processing task/       { print; next }
/WorkerLoop] Pulled task/       { print; next }
/WorkerLoop] Finished task/     { print; next }
/Generated [0-9]+ bytes/        { print; next }
/chunk start=/                  { print; next }
/processed=/                    { print; next }
/\[Leader]/                     { print; next }
/\[TeamLeader/                  { print; next }
/Assigned task/                 { print; next }
/PushWorkerResult/              { print; next }
/\[ClientGateway]/              { print; next }
/\[SessionManager]/             { print; next }
/Loading dataset/               { print; next }
/Progress:/                     { print; next }
' "$INPUT" > "$OUTPUT"

echo "$OUTPUT"
