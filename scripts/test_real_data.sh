#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CLIENT_BIN="$ROOT_DIR/build/src/cpp/mini2_client"
CONFIG="$ROOT_DIR/config/network_setup.json"
DATASET="$ROOT_DIR/test_data/data_10k.csv"

usage() {
    cat <<'EOF'
Usage: test_real_data.sh [--dataset <path>]

Runs the C++ client against the currently running cluster using the provided
CSV (defaults to test_data/data_10k.csv).
EOF
    exit "${1:-1}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--dataset)
            [[ $# -lt 2 ]] && usage
            ARG="$2"
            # If path doesn't contain /, treat as relative to test_data/
            if [[ "$ARG" != */* ]]; then
                DATASET="$ROOT_DIR/test_data/$ARG"
            else
                DATASET="$ARG"
            fi
            shift 2
            ;;
        -h|--help)
            usage 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            ;;
    esac
done

if [[ ! -f "$DATASET" ]]; then
    echo "Dataset not found: $DATASET" >&2
    exit 1
fi

if [[ ! -x "$CLIENT_BIN" ]]; then
    echo "mini2_client binary not found. Build the project before running this test." >&2
    exit 1
fi

echo "Running real-data request"
echo "=========================="
echo "Dataset : $DATASET"
echo "Binary  : $CLIENT_BIN"
echo "Config  : $CONFIG"
echo ""

"$CLIENT_BIN" \
  --mode request \
  --dataset "$DATASET" \
  --config "$CONFIG"

