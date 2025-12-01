#!/usr/bin/env bash
# Helper to start a single node from any working directory.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <node_id>"
    echo "Example: $0 A"
    echo "Nodes: A B C D E F"
    exit 1
fi

NODE=$1
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/src/cpp"
CONFIG_FILE="${CONFIG_FILE:-$ROOT_DIR/config/network_setup.json}"

if [[ ! -x "$BUILD_DIR/mini2_server" ]]; then
    echo "mini2_server binary not found in $BUILD_DIR. Build the project first." >&2
    exit 1
fi

echo "========================================="
echo "Starting Process $NODE"
echo "========================================="
echo ""

case $NODE in
    A) echo "Role: LEADER (Team Green)" ;;
    B) echo "Role: TEAM_LEADER (Team Green)" ;;
    C) echo "Role: WORKER (Team Green)" ;;
    D) echo "Role: TEAM_LEADER (Team Pink)" ;;
    E) echo "Role: TEAM_LEADER (Team Pink)" ;;
    F) echo "Role: WORKER (Team Pink)" ;;
    *) echo "Unknown node: $NODE"; exit 1 ;;
esac

echo ""
echo "Binary : $BUILD_DIR/mini2_server"
echo "Config : $CONFIG_FILE"
echo ""

cd "$BUILD_DIR"
./mini2_server --config "$CONFIG_FILE" --node "$NODE"
