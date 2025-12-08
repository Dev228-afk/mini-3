#!/bin/bash

# Test script to demonstrate configurable timeout feature
# Usage: ./scripts/test_timeout_configs.sh

echo "======================================================================"
echo "Testing Configurable Timeouts for Mini-3"
echo "======================================================================"
echo ""

# Get the binary path
BINARY="../build/src/cpp/mini2_server"
if [ ! -f "$BINARY" ]; then
    echo "Error: Server binary not found at $BINARY"
    echo "Please build the project first: cd build && make"
    exit 1
fi

echo "1. Testing DEFAULT timeouts (no env vars set):"
echo "   Expected: Leader=12000ms, TeamLeader=10000ms"
echo ""
$BINARY --help 2>&1 | head -1 || true
echo ""

echo "2. Testing with CUSTOM SHORT timeouts:"
echo "   Setting: MINI3_LEADER_TIMEOUT_MS=5000"
echo "   Setting: MINI3_TEAMLEADER_TIMEOUT_MS=4000"
echo ""
export MINI3_LEADER_TIMEOUT_MS=5000
export MINI3_TEAMLEADER_TIMEOUT_MS=4000
echo "   To use these values, start servers with:"
echo "   MINI3_LEADER_TIMEOUT_MS=5000 MINI3_TEAMLEADER_TIMEOUT_MS=4000 ./scripts/start_servers.sh"
echo ""

echo "3. Testing with CUSTOM LONG timeouts (for multi-client):"
echo "   Setting: MINI3_LEADER_TIMEOUT_MS=30000"
echo "   Setting: MINI3_TEAMLEADER_TIMEOUT_MS=25000"
echo ""
export MINI3_LEADER_TIMEOUT_MS=30000
export MINI3_TEAMLEADER_TIMEOUT_MS=25000
echo "   To use these values, start servers with:"
echo "   MINI3_LEADER_TIMEOUT_MS=30000 MINI3_TEAMLEADER_TIMEOUT_MS=25000 ./scripts/start_servers.sh"
echo ""

echo "======================================================================"
echo "Summary:"
echo "======================================================================"
echo "The Mini-3 system now supports configurable timeouts via env vars:"
echo ""
echo "  • MINI3_LEADER_TIMEOUT_MS       - Leader wait timeout (default: 12000ms)"
echo "  • MINI3_TEAMLEADER_TIMEOUT_MS   - Team leader wait timeout (default: 10000ms)"
echo ""
echo "Example usage scenarios:"
echo ""
echo "1. FAILURE EXPERIMENTS (short timeouts - current default):"
echo "   No env vars needed, defaults to 12s/10s"
echo ""
echo "2. MULTI-CLIENT SUCCESS DEMO (long timeouts):"
echo "   export MINI3_LEADER_TIMEOUT_MS=30000"
echo "   export MINI3_TEAMLEADER_TIMEOUT_MS=25000"
echo "   ./scripts/start_servers.sh"
echo "   ./scripts/run_multi_clients.sh --dataset test_data/data_1m.csv --clients 4"
echo ""
echo "The timeout values are logged at startup in the server logs."
echo "======================================================================"
