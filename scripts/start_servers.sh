#!/usr/bin/env bash
# Unified launcher for the distributed servers. You can either provide the
# target computer or an explicit node list, otherwise the script uses the host
# IP to decide which trio of servers to start.

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: start_servers.sh [options]

Options:
  -c, --computer <1|2>   Force the Computer 1 (A,B,D) or Computer 2 (C,E,F) profile
  -n, --nodes "A B ..."   Explicit list of nodes to start (overrides --computer)
  -f, --config <path>    Path to network config (default: config/network_setup.json)
  -h, --help             Show this help text

Examples:
  ./scripts/start_servers.sh --computer 1
  ./scripts/start_servers.sh --nodes "A C"
  CONFIG_FILE=config/custom.json ./scripts/start_servers.sh
EOF
        exit "${1:-1}"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

CONFIG_FILE="${CONFIG_FILE:-config/network_setup.json}"
COMPUTER=""
SERVERS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--computer)
            [[ $# -lt 2 ]] && usage
            COMPUTER="$2"
            shift 2
            ;;
        -n|--nodes)
            [[ $# -lt 2 ]] && usage
            read -r -a SERVERS <<< "$2"
            shift 2
            ;;
        -f|--config)
            [[ $# -lt 2 ]] && usage
            CONFIG_FILE="$2"
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

detect_ip() {
    local ip
    ip=$(hostname -I 2>/dev/null | awk '{print $1}') || true
    if [[ -z "${ip:-}" ]]; then
        ip=$(ipconfig getifaddr en0 2>/dev/null || true)
    fi
    if [[ -z "${ip:-}" ]]; then
        ip=$(ip addr show 2>/dev/null | awk '/inet / {print $2}' | cut -d/ -f1 | head -1)
    fi
    echo "$ip"
}

if [[ ${#SERVERS[@]} -eq 0 ]]; then
    case "$COMPUTER" in
        1)
            SERVERS=(A B D)
            ;;
        2)
            SERVERS=(C E F)
            ;;
        "")
            MY_IP=$(detect_ip)
            if [[ -z "$MY_IP" ]]; then
                echo "Unable to detect host IP. Specify --computer or --nodes." >&2
                exit 1
            fi
            if [[ "$MY_IP" =~ 192\.168\.137\. ]] && [[ "$MY_IP" != "192.168.137.1" ]]; then
                COMPUTER=1
                SERVERS=(A B D)
            elif [[ "$MY_IP" =~ (192\.168\.137\.1|169\.254\.|172\.22\.) ]]; then
                COMPUTER=2
                SERVERS=(C E F)
            else
                echo "IP $MY_IP does not match preset ranges. Specify --nodes." >&2
                exit 1
            fi
            ;;
        *)
            echo "Computer must be 1 or 2." >&2
            exit 1
            ;;
    esac
fi

printf "Starting nodes: %s\n" "${SERVERS[*]}"
printf "Using config: %s\n" "$CONFIG_FILE"
printf "Working dir : %s\n" "$ROOT_DIR"

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Missing config file: $CONFIG_FILE" >&2
    exit 1
fi

python3 -m json.tool "$CONFIG_FILE" >/dev/null || {
    echo "Config file is not valid JSON: $CONFIG_FILE" >&2
    exit 1
}

if [[ ! -x build/src/cpp/mini2_server ]]; then
    echo "Binary build/src/cpp/mini2_server not found. Build the project first." >&2
    exit 1
}

mkdir -p logs

echo "Stopping previous mini2_server instances..."
pkill -9 mini2_server >/dev/null 2>&1 || true
sleep 1

SERVER_PIDS=()
for server in "${SERVERS[@]}"; do
    LOG_FILE="logs/server_${server}.log"
    echo "Launching node $server (log: $LOG_FILE)"
    # Suppress gRPC SO_REUSEPORT warnings (not available in WSL1)
    GRPC_VERBOSITY=ERROR ./build/src/cpp/mini2_server --config "$CONFIG_FILE" --node "$server" >"$LOG_FILE" 2>&1 &
    pid=$!
    SERVER_PIDS+=("$server:$pid:$LOG_FILE")
    sleep 1
    if ! ps -p "$pid" >/dev/null 2>&1; then
        echo "Node $server failed to start. See $LOG_FILE" >&2
        exit 1
    fi
done

echo ""
echo "Running processes:"
for entry in "${SERVER_PIDS[@]}"; do
    server="${entry%%:*}"
    rest="${entry#*:}"
    pid="${rest%%:*}"
    log_path="${rest#*:}"
    if ps -p "$pid" >/dev/null 2>&1; then
        printf "  %s (PID %s) - tail %s\n" "$server" "$pid" "$log_path"
    else
        printf "  %s failed to stay running. Check %s\n" "$server" "$log_path"
        exit 1
    fi
done

echo ""
echo "All requested nodes are up. Use 'pkill -f mini2_server' to stop them."
