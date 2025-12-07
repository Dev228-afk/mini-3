#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <combined-slice.log> [output-file]"
  exit 1
fi

IN="$1"
if [ ! -f "$IN" ]; then
  echo "ERROR: input file not found: $IN"
  exit 1
fi

if [ "$#" -eq 2 ]; then
  OUT="$2"
else
  OUT="${IN%.log}-short.log"
fi

# Ultra-clean AWK filter for metrics
awk '
  # Always keep block markers / headers
  /^=====/ { print; next }
  /^#/     { print; next }

  # DROP noisy spam completely
  /WorkerLoop] No tasks available/ { next }
  /RequestTask from/               { next }
  /RequestTaskForWorker/           { next }
  /Updated stats/                  { next }
  /\[Heartbeat]/                   { next }
  /NodeControl]/                   { next }
  /Ping from/                      { next }

  # KEEP important worker lines
  /\[Worker] Processing task/        { print; next }
  /\[WorkerLoop] Pulled task/        { print; next }
  /\[WorkerLoop] Finished task/      { print; next }
  /Generated [0-9]+ bytes/           { print; next }

  # KEEP chunk-level dataset work
  /Loading dataset/                  { print; next }
  /Dataset loaded successfully/      { print; next }
  /chunk start=/                     { print; next }
  /processed=/                       { print; next }

  # KEEP leader / team leader summaries
  /\[Leader]/                         { print; next }
  /\[TeamLeader/                      { print; next }
  /Assigned task/                     { print; next }
  /PushWorkerResult/                  { print; next }

  # KEEP client and session flow
  /\[ClientGateway]/                  { print; next }
  /\[SessionManager]/                 { print; next }

  # FINALLY: ignore everything else
' "$IN" > "$OUT"

echo "$OUT"
