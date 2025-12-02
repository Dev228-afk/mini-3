ch #!/usr/bin/env bash
# Wrapper script for mini2_client with connection retry logic

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

# Suppress gRPC warnings
export GRPC_VERBOSITY=ERROR

CLIENT_BIN="build/src/cpp/mini2_client"

if [[ ! -x "$CLIENT_BIN" ]]; then
    echo "Error: Client binary not found at $CLIENT_BIN" >&2
    echo "Build the project first with: ./scripts/build.sh" >&2
    exit 1
fi

# Extract server address from arguments
SERVER=""
for ((i=1; i<=$#; i++)); do
    if [[ "${!i}" == "--server" || "${!i}" == "--gateway" ]]; then
        ((i++))
        SERVER="${!i}"
        break
    fi
done

# If server specified, check connectivity first
if [[ -n "$SERVER" ]]; then
    HOST="${SERVER%%:*}"
    PORT="${SERVER##*:}"
    
    echo "Checking connection to $SERVER..."
    
    # Try to connect with timeout
    MAX_RETRIES=5
    RETRY=0
    CONNECTED=false
    
    while [[ $RETRY -lt $MAX_RETRIES ]]; do
        ((RETRY++))
        echo "  Attempt $RETRY/$MAX_RETRIES..."
        
        # Use timeout and nc to check if port is open
        if command -v timeout >/dev/null 2>&1; then
            if timeout 2 bash -c "echo >/dev/tcp/$HOST/$PORT" 2>/dev/null; then
                CONNECTED=true
                echo "  ✓ Connected to $SERVER"
                break
            fi
        elif command -v nc >/dev/null 2>&1; then
            if nc -z -w 2 "$HOST" "$PORT" 2>/dev/null; then
                CONNECTED=true
                echo "  ✓ Connected to $SERVER"
                break
            fi
        else
            # Fallback: just try the client
            echo "  Note: nc/timeout not available, proceeding anyway"
            CONNECTED=true
            break
        fi
        
        if [[ $RETRY -lt $MAX_RETRIES ]]; then
            echo "  Connection failed, retrying in 2s..."
            sleep 2
        fi
    done
    
    if [[ "$CONNECTED" == "false" ]]; then
        echo ""
        echo "ERROR: Could not connect to $SERVER after $MAX_RETRIES attempts" >&2
        echo ""
        echo "Troubleshooting steps:" >&2
        echo "  1. Verify server is running: ps aux | grep mini2_server" >&2
        echo "  2. Check if port is listening: netstat -an | grep $PORT" >&2
        echo "  3. Test network connectivity: ping $HOST" >&2
        echo "  4. Check firewall rules (Windows/WSL may need special config)" >&2
        exit 1
    fi
    echo ""
fi

# Run the client with all arguments
exec "$CLIENT_BIN" "$@"
