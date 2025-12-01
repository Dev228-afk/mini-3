#!/usr/bin/env bash
# Runs the real-data request flow end-to-end. Accepts an optional dataset path.

set -euo pipefail

usage() {
	cat <<'EOF'
Usage: test_real_data.sh [--dataset <path>]

Runs the C++ client against the currently running cluster using the provided
CSV (defaults to Data/2020-fire/merged.csv).
EOF
	exit "${1:-1}"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/src/cpp"
DATASET="$ROOT_DIR/test_data/data_10k.csv"

while [[ $# -gt 0 ]]; do
	case "$1" in
		-d|--dataset)
			[[ $# -lt 2 ]] && usage
			DATASET="$2"
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

if [[ ! -x "$BUILD_DIR/mini2_client" ]]; then
	echo "mini2_client binary not found. Build the project before running this test." >&2
	exit 1
fi

echo "Running real-data request"
echo "=========================="
echo "Dataset : $DATASET"
echo "Binary  : $BUILD_DIR/mini2_client"
echo ""

cd "$BUILD_DIR"
./mini2_client --mode request --dataset "$DATASET"

