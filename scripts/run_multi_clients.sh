#!/usr/bin/env bash
set -euo pipefail

# Run N concurrent clients against the gateway for a given dataset,
# after stopping and restarting local servers.
#
# Usage:
#   ./scripts/run_multi_clients.sh --dataset test_data/data_1m.csv --clients 4 --label mc_1m_c4 --computer 1
#
# Notes:
#   - This script only manages servers on the *local* machine.
#   - You must ensure the other PC's servers are running (e.g., by running
#     ./scripts/start_servers.sh --computer 2 there).
#   - Clients are run via scripts/test_real_data.sh, which prints a Strategy B
#     results block that we parse for metrics.

usage() {
  echo "Usage: $0 --dataset <path> --clients <N> --label <run_label> [--computer <id>]"
  echo "  --dataset   Dataset path, e.g. test_data/data_1m.csv"
  echo "  --clients   Number of concurrent clients to run"
  echo "  --label     Label for this run (used in log filenames)"
  echo "  --computer  Optional: value passed to start_servers.sh (default: 1)"
  exit 1
}

DATASET=""
CLIENTS=""
LABEL=""
COMPUTER="1"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dataset)
      DATASET="$2"; shift 2;;
    --clients)
      CLIENTS="$2"; shift 2;;
    --label)
      LABEL="$2"; shift 2;;
    --computer)
      COMPUTER="$2"; shift 2;;
    -h|--help)
      usage;;
    *)
      echo "Unknown argument: $1"; usage;;
  esac
done

if [[ -z "${DATASET}" || -z "${CLIENTS}" || -z "${LABEL}" ]]; then
  echo "ERROR: missing required arguments."
  usage
fi

if ! [[ "${CLIENTS}" =~ ^[0-9]+$ ]]; then
  echo "ERROR: --clients must be an integer."
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

LOG_DIR="${ROOT_DIR}/logs"
mkdir -p "${LOG_DIR}"

echo "[multi] Root dir : ${ROOT_DIR}"
echo "[multi] Dataset  : ${DATASET}"
echo "[multi] Clients  : ${CLIENTS}"
echo "[multi] Label    : ${LABEL}"
echo "[multi] Computer : ${COMPUTER}"
echo

# ---- Stop local servers ----
echo "[multi] Stopping any existing mini2_server processes on this machine..."
pkill -f "mini2_server" || true
sleep 1

# ---- Clear logs & start local servers ----
echo "[multi] Clearing logs..."
./scripts/clear_logs.sh

echo "[multi] Starting servers on this machine (computer=${COMPUTER})..."
./scripts/start_servers.sh --computer "${COMPUTER}"
echo "[multi] Waiting a bit for servers to be ready..."
sleep 3

# ---- Run N clients in parallel ----
echo "[multi] Launching ${CLIENTS} client(s) in parallel..."
START_TS_MS=$(date +%s%3N)

PIDS=()
CLIENT_LOGS=()

for i in $(seq 1 "${CLIENTS}"); do
  LOG="${LOG_DIR}/multi_client_${LABEL}_client${i}.log"
  CLIENT_LOGS+=("${LOG}")
  echo "[multi]  -> client ${i}: logging to ${LOG}"
  # Run the real-data script; capture both stdout and stderr
  ./scripts/test_real_data.sh --dataset "${DATASET}" > "${LOG}" 2>&1 &
  PIDS+=($!)
done

# Wait for all clients to finish
FAIL=0
for pid in "${PIDS[@]}"; do
  if ! wait "${pid}"; then
    echo "[multi] WARNING: one client exited with non-zero status."
    FAIL=1
  fi
done

END_TS_MS=$(date +%s%3N)
TOTAL_MS=$((END_TS_MS - START_TS_MS))
echo
echo "[multi] All clients finished. Wall-clock time: ${TOTAL_MS} ms"

# ---- Parse metrics from logs ----

# Function to extract a numeric value from a given pattern line
extract_time_ms() {
  local line="$1"
  # expects: "Time to first chunk: 11.623 s" or "... 15346 ms"
  if [[ "${line}" =~ ([0-9]+(\.[0-9]+)?)\ ?s ]]; then
    # seconds -> ms
    python3 - <<EOF
val = float("${BASH_REMATCH[1]}")
print(int(val * 1000.0))
EOF
  elif [[ "${line}" =~ ([0-9]+(\.[0-9]+)?)\ ?ms ]]; then
    python3 - <<EOF
val = float("${BASH_REMATCH[1]}")
print(int(val))
EOF
  else
    echo ""
  fi
}

extract_number() {
  local line="$1"
  # extract integer (e.g., "Total chunks: 9" or "Total bytes: 185739627")
  if [[ "${line}" =~ ([0-9]+) ]]; then
    echo "${BASH_REMATCH[1]}"
  else
    echo ""
  fi
}

extract_float() {
  local line="$1"
  # extract float (e.g., "Effective throughput: 12.10 MB/s")
  if [[ "${line}" =~ ([0-9]+(\.[0-9]+)?) ]]; then
    echo "${BASH_REMATCH[1]}"
  else
    echo ""
  fi
}

echo
echo "[multi] Per-client results:"
printf "%-8s %-12s %-12s %-12s %-14s %-12s\n" "client" "ttfb_ms" "total_ms" "chunks" "total_bytes" "MBps"

TOTAL_TTFB=0
TOTAL_TOTAL=0
TOTAL_BYTES_SUM=0
COUNT_OK=0

i=0
for LOG in "${CLIENT_LOGS[@]}"; do
  i=$((i + 1))

  # Extract lines
  TTFB_LINE=$(grep -m1 "Time to first chunk:" "${LOG}" || true)
  TOTAL_LINE=$(grep -m1 "Total time:" "${LOG}" || true)
  CHUNKS_LINE=$(grep -m1 "Total chunks:" "${LOG}" || true)
  BYTES_LINE=$(grep -m1 "Total bytes:" "${LOG}" || true)
  THR_LINE=$(grep -m1 "Effective throughput:" "${LOG}" || true)

  TTFB_MS=""
  TOTAL_MS_CL=""
  CHUNKS=""
  BYTES=""
  MBPS=""

  if [[ -n "${TTFB_LINE}" ]]; then
    TTFB_MS=$(extract_time_ms "${TTFB_LINE}")
  fi
  if [[ -n "${TOTAL_LINE}" ]]; then
    TOTAL_MS_CL=$(extract_time_ms "${TOTAL_LINE}")
  fi
  if [[ -n "${CHUNKS_LINE}" ]]; then
    CHUNKS=$(extract_number "${CHUNKS_LINE}")
  fi
  if [[ -n "${BYTES_LINE}" ]]; then
    BYTES=$(extract_number "${BYTES_LINE}")
  fi
  if [[ -n "${THR_LINE}" ]]; then
    MBPS=$(extract_float "${THR_LINE}")
  fi

  # Aggregate stats only if we have total time
  if [[ -n "${TOTAL_MS_CL}" ]]; then
    COUNT_OK=$((COUNT_OK + 1))
    TOTAL_TTFB=$((TOTAL_TTFB + ${TTFB_MS:-0}))
    TOTAL_TOTAL=$((TOTAL_TOTAL + TOTAL_MS_CL))
  fi
  if [[ -n "${BYTES}" ]]; then
    TOTAL_BYTES_SUM=$((TOTAL_BYTES_SUM + BYTES))
  fi

  printf "%-8s %-12s %-12s %-12s %-14s %-12s\n" \
    "${i}" "${TTFB_MS:-?}" "${TOTAL_MS_CL:-?}" "${CHUNKS:-?}" "${BYTES:-?}" "${MBPS:-?}"
done

echo
echo "[multi] Aggregate across ${CLIENTS} clients (only counting those with parsed metrics):"
if [[ "${COUNT_OK}" -gt 0 ]]; then
  AVG_TTFB=$((TOTAL_TTFB / COUNT_OK))
  AVG_TOTAL=$((TOTAL_TOTAL / COUNT_OK))
else
  AVG_TTFB=0
  AVG_TOTAL=0
fi

# Overall throughput based on wall-clock and total bytes
if [[ "${TOTAL_MS}" -gt 0 ]]; then
  # MB = bytes / (1024*1024)
  OVERALL_MBPS=$(python3 - <<EOF
total_bytes = ${TOTAL_BYTES_SUM}
wall_ms = ${TOTAL_MS}
if wall_ms <= 0:
    print("0.0")
else:
    mb = total_bytes / (1024.0*1024.0)
    print(f"{mb / (wall_ms/1000.0):.2f}")
EOF
)
else
  OVERALL_MBPS="0.0"
fi

echo "  AVG ttfb_ms    : ${AVG_TTFB}"
echo "  AVG total_ms   : ${AVG_TOTAL}"
echo "  SUM total_bytes: ${TOTAL_BYTES_SUM}"
echo "  Wall time ms   : ${TOTAL_MS}"
echo "  Overall MB/s   : ${OVERALL_MBPS}"

if [[ "${FAIL}" -ne 0 ]]; then
  echo "[multi] WARNING: some clients failed (non-zero exit). Check individual logs."
fi
