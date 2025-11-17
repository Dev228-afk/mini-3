#!/bin/bash
# Run ALL tests quickly - use this after servers are started

# Gateway server (adjust if needed)
GATEWAY="192.168.137.169:50050"
CLIENT="./build/src/cpp/mini2_client"

echo "========== QUICK TEST ALL PHASES =========="
echo ""

echo "Phase 2 Test 1: GREEN team"
$CLIENT --server $GATEWAY --query "test green" --need-green
echo ""

echo "Phase 2 Test 2: PINK team"
$CLIENT --server $GATEWAY --query "test pink" --need-pink
echo ""

echo "Phase 2 Test 3: Both teams"
$CLIENT --server $GATEWAY --query "test both" --need-green --need-pink
echo ""

echo "Phase 3 Test: Small dataset"
if [ -f test_data/data_1k.csv ]; then
    $CLIENT --server $GATEWAY --dataset test_data/data_1k.csv --query "all"
else
    echo "SKIP: test_data/data_1k.csv not found"
fi
echo ""

echo "Phase 4: Check shared memory (Computer 1)"
if [ -f /dev/shm/shm_host1 ]; then
    ./build/src/cpp/inspect_shm shm_host1
else
    echo "Shared memory not found on this computer"
fi

echo ""
echo "Check logs:"
echo "  tail -50 logs/server_A.log | grep -E 'healthy|request|session'"
