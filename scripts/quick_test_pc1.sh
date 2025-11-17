#!/bin/bash
# ============================================================
# Quick Test Script for PC-1
# Run this on root@DESKTOP-F0TMIC2
# ============================================================

set -e

echo "============================================================"
echo "PC-1 Quick Test Script"
echo "============================================================"
echo ""

# Change to project directory
cd /home/meghpatel/dev/mini-2

# Check if servers are running
SERVER_COUNT=$(ps aux | grep mini2_server | grep -v grep | wc -l)
if [ "$SERVER_COUNT" -lt 3 ]; then
    echo "⚠️  Servers not running. Starting servers A, B, D..."
    ./build/src/cpp/mini2_server A > /tmp/server_A.log 2>&1 &
    ./build/src/cpp/mini2_server B > /tmp/server_B.log 2>&1 &
    ./build/src/cpp/mini2_server D > /tmp/server_D.log 2>&1 &
    echo "✓ Servers started. Waiting 15 seconds for health checks..."
    sleep 15
else
    echo "✓ Servers already running (count: $SERVER_COUNT)"
    echo ""
fi

# Determine dataset to test (default: 1K)
DATASET=${1:-test_data/data_1k.csv}

echo "============================================================"
echo "Running test with dataset: $DATASET"
echo "============================================================"
echo ""

# Run the test
./build/src/cpp/mini2_client \
  --server 169.254.239.138:50050 \
  --mode session \
  --query "$DATASET"

echo ""
echo "============================================================"
echo "Server A Logs (Last 20 lines)"
echo "============================================================"
tail -20 /tmp/server_A.log | grep -E "(Processing|Completed|Waiting|Received|Background)"

echo ""
echo "============================================================"
echo "Server B Logs (Team Leader Green)"
echo "============================================================"
tail -10 /tmp/server_B.log | grep -E "(Handling|Loading|Received)"

echo ""
echo "============================================================"
echo "Server D Logs (Worker)"
echo "============================================================"
tail -10 /tmp/server_D.log | grep -E "(Handling|Processing|Worker)"

echo ""
echo "============================================================"
echo "Health Check Status"
echo "============================================================"
echo "Server A:"
tail -5 /tmp/server_A.log | grep "Health check complete" || echo "  (no recent health check)"
echo "Server B:"
tail -5 /tmp/server_B.log | grep "Health check complete" || echo "  (no recent health check)"
echo "Server D:"
tail -5 /tmp/server_D.log | grep "Health check complete" || echo "  (no recent health check)"

echo ""
echo "============================================================"
echo "Test Complete!"
echo "============================================================"
echo ""
echo "To test different datasets:"
echo "  ./scripts/quick_test_pc1.sh test_data/data_10k.csv"
echo "  ./scripts/quick_test_pc1.sh test_data/data_100k.csv"
echo "  ./scripts/quick_test_pc1.sh test_data/data_1m.csv"
echo ""
