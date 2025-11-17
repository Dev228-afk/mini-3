#!/bin/bash
# Comprehensive test script - runs ALL phases with detailed output

# Gateway server (adjust if needed)
GATEWAY="192.168.137.189:50050"
CLIENT="./build/src/cpp/mini2_client"

echo "============================================"
echo "COMPREHENSIVE TEST SUITE - ALL PHASES"
echo "Gateway: $GATEWAY"
echo "============================================"
echo ""

# Check if client exists
if [ ! -f "$CLIENT" ]; then
    echo "ERROR: Client not found at $CLIENT"
    exit 1
fi

# Check if servers are running
SERVER_COUNT=$(ps aux | grep mini2_server | grep -v grep | wc -l)
echo "Servers running: $SERVER_COUNT"
if [ "$SERVER_COUNT" -eq 0 ]; then
    echo "ERROR: No servers running! Start servers first."
    exit 1
fi
echo ""

# ===================
# PHASE 1: Health Checks
# ===================
echo "=========================================="
echo "PHASE 1: Network Communication & Health"
echo "=========================================="
echo ""
echo "Waiting 15 seconds for health checks to complete..."
sleep 15

echo "Checking server A logs for health status:"
if [ -f logs/server_A.log ]; then
    tail -30 logs/server_A.log | grep -i "health" | tail -5
else
    echo "  [WARNING] No logs found for server A"
fi
echo ""

# ===================
# PHASE 2: Request Forwarding
# ===================
echo "=========================================="
echo "PHASE 2: Request Forwarding & Aggregation"
echo "=========================================="
echo ""

echo "Test 2.1: Simple Query (No Teams)"
echo "-----------------------------------"
$CLIENT --server $GATEWAY --query "phase2 simple query test" 2>&1 | head -15
echo ""
sleep 2

echo "Test 2.2: GREEN Team Request (Cross-Computer to C)"
echo "----------------------------------------------------"
echo "Expected flow: Client → A (Cmp1) → B (Cmp1) → C (Cmp2)"
$CLIENT --server $GATEWAY --query "green team cross network test" --need-green 2>&1 | head -15
echo ""
sleep 2

echo "Test 2.3: PINK Team Request (Cross-Computer to E)"
echo "----------------------------------------------------"
echo "Expected flow: Client → A (Cmp1) → E (Cmp2) → F (Cmp2)"
$CLIENT --server $GATEWAY --query "pink team cross network test" --need-pink 2>&1 | head -15
echo ""
sleep 2

echo "Test 2.4: Both Teams Request (Parallel Processing)"
echo "----------------------------------------------------"
echo "Expected: Both GREEN and PINK process simultaneously"
$CLIENT --server $GATEWAY --query "both teams parallel test" --need-green --need-pink 2>&1 | head -15
echo ""
sleep 2

# ===================
# PHASE 3: Chunked Responses
# ===================
echo "=========================================="
echo "PHASE 3: Chunked Response Strategies"
echo "=========================================="
echo ""

echo "Test 3.1: Small Dataset (1K rows - should be single response)"
echo "---------------------------------------------------------------"
if [ -f test_data/data_1k.csv ]; then
    $CLIENT --server $GATEWAY --dataset test_data/data_1k.csv --query "all" 2>&1 | head -15
    echo ""
else
    echo "[SKIP] test_data/data_1k.csv not found"
    echo "Generate with: cd test_data && python3 data.py"
    echo ""
fi
sleep 2

echo "Test 3.2: Medium Dataset (10K rows - chunked)"
echo "-----------------------------------------------"
if [ -f test_data/data_10k.csv ]; then
    $CLIENT --server $GATEWAY --dataset test_data/data_10k.csv --query "all" --mode chunked 2>&1 | head -15
    echo ""
else
    echo "[SKIP] test_data/data_10k.csv not found"
    echo ""
fi
sleep 2

echo "Test 3.3: Memory Usage Check"
echo "------------------------------"
echo "Memory at gateway (Server A):"
ps aux | grep "mini2_server A" | grep -v grep | awk '{print "  RSS Memory: " $6 " KB"}'
echo ""

# ===================
# PHASE 4: Shared Memory
# ===================
echo "=========================================="
echo "PHASE 4: Shared Memory Coordination"
echo "=========================================="
echo ""

echo "Test 4.1: Inspect Shared Memory Segments"
echo "------------------------------------------"
if [ -f ./build/src/cpp/inspect_shm ]; then
    # Check Computer 1 segment
    if [ -f /dev/shm/shm_host1 ]; then
        echo "Shared Memory Segment: shm_host1 (Computer 1 - A, B, D)"
        ./build/src/cpp/inspect_shm shm_host1 2>&1 | head -30
        echo ""
    else
        echo "[INFO] shm_host1 not found (expected on Computer 1 only)"
        echo ""
    fi
    
    # Check Computer 2 segment
    if [ -f /dev/shm/shm_host2 ]; then
        echo "Shared Memory Segment: shm_host2 (Computer 2 - C, E, F)"
        ./build/src/cpp/inspect_shm shm_host2 2>&1 | head -30
        echo ""
    else
        echo "[INFO] shm_host2 not found (expected on Computer 2 only)"
        echo ""
    fi
else
    echo "[WARNING] inspect_shm tool not found"
    echo ""
fi

echo "Test 4.2: Load-Aware Routing Test"
echo "-----------------------------------"
echo "Sending 5 concurrent GREEN team requests to test load distribution..."
for i in {1..5}; do
    $CLIENT --server $GATEWAY --query "load test request $i" --need-green > /dev/null 2>&1 &
done
sleep 3
echo "Check logs for load-aware routing decisions:"
grep -i "load" logs/server_A.log 2>/dev/null | tail -5 || echo "  No load-aware routing logs found"
echo ""

# ===================
# RESULTS SUMMARY
# ===================
echo "=========================================="
echo "TEST RESULTS SUMMARY"
echo "=========================================="
echo ""

echo "Phase 1: Health Checks"
if grep -q "healthy" logs/server_A.log 2>/dev/null; then
    echo "  ✓ PASS - Health checks working"
else
    echo "  ✗ FAIL - No health check logs found"
fi

echo "Phase 2: Request Forwarding"
echo "  (Check output above for connection status)"

echo "Phase 3: Chunked Responses"
if [ -f test_data/data_1k.csv ]; then
    echo "  ✓ PASS - Test data available"
else
    echo "  ⚠ WARN - Test data not found"
fi

echo "Phase 4: Shared Memory"
if [ -f /dev/shm/shm_host1 ] || [ -f /dev/shm/shm_host2 ]; then
    echo "  ✓ PASS - Shared memory segments created"
else
    echo "  ⚠ WARN - No shared memory segments found"
fi

echo ""
echo "=========================================="
echo "PERFORMANCE METRICS"
echo "=========================================="
echo ""

echo "Server Process Status:"
ps aux | grep mini2_server | grep -v grep | awk '{print $11, "(PID " $2 ") - Memory:", $6, "KB"}'

echo ""
echo "Network Latency (approximate):"
echo "  Computer 1 to Computer 2:"
ping -c 3 192.168.137.1 2>/dev/null | tail -1 || echo "  [Unable to measure]"

echo ""
echo "=========================================="
echo "LOG FILES FOR REVIEW"
echo "=========================================="
ls -lh logs/*.log 2>/dev/null || echo "No log files found"

echo ""
echo "=========================================="
echo "TEST COMPLETE"
echo "=========================================="
echo ""
echo "To view detailed logs:"
echo "  tail -100 logs/server_A.log"
echo "  grep ERROR logs/*.log"
echo "  grep 'session\\|request\\|forward' logs/server_A.log"
