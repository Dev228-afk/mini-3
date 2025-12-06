#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "Usage: $0 DATASET_PATH LABEL"
  echo "Example: $0 test_data/data_1m.csv data1m"
  exit 1
fi

DATASET="$1"
LABEL="$2"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$ROOT_DIR"

echo "[run_experiment_pc1] Clearing logs..."
./scripts/clear_logs.sh

echo "[run_experiment_pc1] Starting servers for computer 1 (A,B,D)..."
./scripts/start_servers.sh --computer 1

echo "[run_experiment_pc1] Marking current log positions..."
MARK_FILE="$(./scripts/mark_logs.sh "$LABEL")"
echo "[run_experiment_pc1] MARK_FILE=$MARK_FILE"

echo "[run_experiment_pc1] Running client for dataset: $DATASET"
./scripts/test_real_data.sh --dataset "$DATASET"

echo "[run_experiment_pc1] Client finished. Slicing logs since mark..."
SLICE_FILE="$(./scripts/slice_since_mark.sh "$MARK_FILE" "$LABEL")"
echo "[run_experiment_pc1] Combined log slice: $SLICE_FILE"
