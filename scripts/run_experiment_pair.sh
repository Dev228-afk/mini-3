#!/usr/bin/env bash
set -euo pipefail

# ========= CONFIG (edit for your PCs) =========
# pc-1: where you run this script
LOCAL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# pc-2 SSH target + project path
# Change these if your username/host are different
REMOTE_HOST="dev@Dev"
REMOTE_ROOT="/mnt/c/Users/devan/mini-3"
# =============================================

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <dataset-path> <label>"
  echo "Example: $0 test_data/data_1m.csv data1m"
  exit 1
fi

DATASET="$1"
LABEL="$2"

echo "[pair] Using dataset: $DATASET"
echo "[pair] Label       : $LABEL"
echo "[pair] Local root  : $LOCAL_ROOT"
echo "[pair] Remote root : $REMOTE_HOST:$REMOTE_ROOT"

mkdir -p "$LOCAL_ROOT/logs"

# ---------- 1. Check SSH and remote setup ----------
echo "[pair] Checking SSH connection to $REMOTE_HOST..."
if ! ssh -o BatchMode=yes -o ConnectTimeout=8 "$REMOTE_HOST" "echo '[pair] SSH OK on ' \$(hostname)"; then
  echo "[pair] ERROR: SSH to $REMOTE_HOST failed."
  echo "[pair] Hint: from pc-1 run 'ssh $REMOTE_HOST' manually to set up keys/known_hosts."
  exit 1
fi

echo "[pair] Checking remote project folder..."
if ! ssh "$REMOTE_HOST" "cd '$REMOTE_ROOT' 2>/dev/null"; then
  echo "[pair] ERROR: Directory $REMOTE_ROOT not found on $REMOTE_HOST."
  exit 1
fi

echo "[pair] Checking remote run_experiment_pc2.sh..."
if ! ssh "$REMOTE_HOST" "cd '$REMOTE_ROOT' && test -x scripts/run_experiment_pc2.sh"; then
  echo "[pair] ERROR: scripts/run_experiment_pc2.sh not found or not executable on remote."
  exit 1
fi

echo "[pair] Checking local run_experiment_pc1.sh..."
if [[ ! -x "$LOCAL_ROOT/scripts/run_experiment_pc1.sh" ]]; then
  echo "[pair] ERROR: scripts/run_experiment_pc1.sh not found or not executable locally."
  exit 1
fi

# ---------- 2. Start remote half (pc-2) ----------
REMOTE_LOG="$LOCAL_ROOT/logs/remote-${LABEL}-$(date +%Y%m%d-%H%M%S).log"
echo "[pair] Starting remote half (pc-2), logging to $REMOTE_LOG..."

# Run remote script in background, capturing its stdout/stderr
set +e
ssh "$REMOTE_HOST" "cd '$REMOTE_ROOT' && ./scripts/run_experiment_pc2.sh '$LABEL'" \
  >"$REMOTE_LOG" 2>&1 &
REMOTE_PID=$!
set -e

# ---------- 3. Run local half (pc-1) ----------
echo "[pair] Running local half (pc-1)..."
set +e
"$LOCAL_ROOT/scripts/run_experiment_pc1.sh" "$DATASET" "$LABEL"
LOCAL_STATUS=$?
set -e

if [[ $LOCAL_STATUS -ne 0 ]]; then
  echo "[pair] ERROR: run_experiment_pc1.sh failed (exit $LOCAL_STATUS)."
  echo "[pair] Attempting to stop remote job..."
  kill "$REMOTE_PID" 2>/dev/null || true
  exit $LOCAL_STATUS
fi

echo "[pair] Local run finished. Waiting for remote pc-2 to finish..."
wait "$REMOTE_PID"
REMOTE_STATUS=$?

if [[ $REMOTE_STATUS -ne 0 ]]; then
  echo "[pair] ERROR: remote run_experiment_pc2.sh failed (exit $REMOTE_STATUS)."
  echo "[pair] See $REMOTE_LOG for remote details."
  exit $REMOTE_STATUS
fi

echo "[pair] Both sides finished successfully."
echo "[pair] Collecting latest log slices for label '$LABEL'..."

# ---------- 4. Find latest slice files ----------
# Local slice (pc-1)
LOCAL_SLICE=$(ls -1t "$LOCAL_ROOT"/logs/slice-*-"$LABEL".log 2>/dev/null | head -n1 || true)
if [[ -z "${LOCAL_SLICE:-}" ]]; then
  echo "[pair] WARNING: No local slice for label '$LABEL' found (pattern logs/slice-*-$LABEL.log)."
  echo "[pair] Nothing more to do."
  exit 0
fi

# Remote slice (pc-2) – relative path inside project
REMOTE_SLICE_REL=$(ssh "$REMOTE_HOST" "cd '$REMOTE_ROOT' && ls -1t logs/slice-*-'$LABEL'.log 2>/dev/null | head -n1 || true")

if [[ -z "${REMOTE_SLICE_REL:-}" ]]; then
  echo "[pair] WARNING: No remote slice for label '$LABEL' found on $REMOTE_HOST."
  echo "[pair] Only local logs will be used."
  COMBINED="$LOCAL_ROOT/logs/combined-${LABEL}-$(date +%Y%m%d-%H%M%S).log"
  cp "$LOCAL_SLICE" "$COMBINED"
  echo "[pair] Combined (local-only) log written to: $COMBINED"
  exit 0
fi

echo "[pair] Local slice : $LOCAL_SLICE"
echo "[pair] Remote slice: $REMOTE_SLICE_REL"

# ---------- 5. Copy remote slice and build combined ----------
echo "[pair] Copying remote slice to local..."
scp "$REMOTE_HOST":"$REMOTE_ROOT/$REMOTE_SLICE_REL" "$LOCAL_ROOT/logs/" || {
  echo "[pair] ERROR: scp of remote slice failed; keeping runs separate."
  exit 1
}

REMOTE_SLICE_LOCAL="$LOCAL_ROOT/logs/$(basename "$REMOTE_SLICE_REL")"
COMBINED="$LOCAL_ROOT/logs/combined-${LABEL}-$(date +%Y%m%d-%H%M%S).log"

{
  echo "# Combined logs for $LABEL at $(date)"
  echo
  echo "===== LOCAL (pc-1) slice: $(basename "$LOCAL_SLICE") ====="
  cat "$LOCAL_SLICE"
  echo
  echo "===== REMOTE (pc-2) slice: $(basename "$REMOTE_SLICE_LOCAL") ====="
  cat "$REMOTE_SLICE_LOCAL"
} > "$COMBINED"

echo "[pair] Combined log written to: $COMBINED"
echo "[pair] Done."
