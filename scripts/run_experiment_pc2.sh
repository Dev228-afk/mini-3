#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 LABEL"
  echo "Example: $0 data1m"
  exit 1
fi

LABEL="$1"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$ROOT_DIR"

echo "[run_experiment_pc2] Clearing logs..."
./scripts/clear_logs.sh

echo "[run_experiment_pc2] Starting servers for computer 2 (C,E,F)..."
./scripts/start_servers.sh --computer 2

echo "[run_experiment_pc2] Marking current log positions..."
MARK_FILE="$(./scripts/mark_logs.sh "$LABEL")"
echo "[run_experiment_pc2] MARK_FILE=$MARK_FILE"

echo
echo "[run_experiment_pc2] >>> Now run the client on PC-1 for dataset label '$LABEL'."
echo "[run_experiment_pc2] When the client finishes, run this command on PC-2 to slice logs:"
echo "  ./scripts/slice_since_mark.sh \"$MARK_FILE\" \"$LABEL\""
echo
