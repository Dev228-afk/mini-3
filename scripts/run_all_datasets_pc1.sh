#!/bin/bash
# ============================================================
# Run All Datasets Test - PC-1
# Tests 1K, 10K, 100K, 1M datasets and collects metrics
# ============================================================

set -e

echo "============================================================"
echo "PC-1 Complete Dataset Testing with Metrics"
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

# Array of datasets to test
DATASETS=(
    "test_data/data_1k.csv"
    "test_data/data_10k.csv"
    "test_data/data_100k.csv"
    "test_data/data_1m.csv"
)

# Results storage
declare -a RESULTS_DATASET
declare -a RESULTS_CHUNKS
declare -a RESULTS_BYTES
declare -a RESULTS_FIRST_CHUNK_MS
declare -a RESULTS_TOTAL_MS

echo "============================================================"
echo "Starting Sequential Dataset Tests"
echo "============================================================"
echo ""

# Run tests for each dataset
for i in "${!DATASETS[@]}"; do
    DATASET="${DATASETS[$i]}"
    DATASET_NAME=$(basename "$DATASET" .csv)
    
    echo "------------------------------------------------------------"
    echo "Test $((i+1))/${#DATASETS[@]}: $DATASET_NAME"
    echo "------------------------------------------------------------"
    
    # Run the test and capture output
    OUTPUT=$(./build/src/cpp/mini2_client \
        --server 169.254.239.138:50050 \
        --mode session \
        --query "$DATASET" 2>&1)
    
    echo "$OUTPUT"
    echo ""
    
    # Extract metrics using grep and awk
    CHUNKS=$(echo "$OUTPUT" | grep "Total chunks:" | awk '{print $3}')
    BYTES=$(echo "$OUTPUT" | grep "Total bytes:" | awk '{print $3}')
    FIRST_CHUNK=$(echo "$OUTPUT" | grep "Time to first chunk:" | awk '{print $5}')
    TOTAL_TIME=$(echo "$OUTPUT" | grep "^Total time:" | awk '{print $3}')
    
    # Store results
    RESULTS_DATASET[$i]="$DATASET_NAME"
    RESULTS_CHUNKS[$i]="${CHUNKS:-N/A}"
    RESULTS_BYTES[$i]="${BYTES:-N/A}"
    RESULTS_FIRST_CHUNK_MS[$i]="${FIRST_CHUNK:-N/A}"
    RESULTS_TOTAL_MS[$i]="${TOTAL_TIME:-N/A}"
    
    # Show brief summary
    echo "  📊 Chunks: $CHUNKS, Bytes: $BYTES"
    echo "  ⚡ First chunk: ${FIRST_CHUNK}ms, Total: ${TOTAL_TIME}ms"
    echo ""
    
    # Small delay between tests
    sleep 2
done

echo "============================================================"
echo "COMPREHENSIVE TEST RESULTS SUMMARY"
echo "============================================================"
echo ""
printf "%-15s | %8s | %12s | %15s | %12s\n" "Dataset" "Chunks" "Bytes" "First Chunk" "Total Time"
printf "%-15s-|-%8s-|-%12s-|-%15s-|-%12s\n" "---------------" "--------" "------------" "---------------" "------------"

for i in "${!DATASETS[@]}"; do
    printf "%-15s | %8s | %12s | %12s ms | %9s ms\n" \
        "${RESULTS_DATASET[$i]}" \
        "${RESULTS_CHUNKS[$i]}" \
        "${RESULTS_BYTES[$i]}" \
        "${RESULTS_FIRST_CHUNK_MS[$i]}" \
        "${RESULTS_TOTAL_MS[$i]}"
done

echo ""
echo "============================================================"
echo "Server Health Status"
echo "============================================================"
echo "Server A:"
tail -3 /tmp/server_A.log | grep "Health check complete" || echo "  (no recent health check)"
echo ""
echo "Server B:"
tail -3 /tmp/server_B.log | grep "Health check complete" || echo "  (no recent health check)"
echo ""
echo "Server D:"
tail -3 /tmp/server_D.log | grep "Health check complete" || echo "  (no recent health check)"

echo ""
echo "============================================================"
echo "Recent Processing Activity (Server A)"
echo "============================================================"
tail -15 /tmp/server_A.log | grep -E "(Processing|Completed|Waiting|Received)" || echo "No recent activity"

echo ""
echo "============================================================"
echo "Performance Analysis"
echo "============================================================"

# Calculate throughput for largest dataset
if [ "${RESULTS_TOTAL_MS[3]}" != "N/A" ] && [ "${RESULTS_BYTES[3]}" != "N/A" ]; then
    TOTAL_SEC=$(echo "scale=3; ${RESULTS_TOTAL_MS[3]} / 1000" | bc)
    THROUGHPUT=$(echo "scale=2; ${RESULTS_BYTES[3]} / $TOTAL_SEC / 1024" | bc)
    echo "1M Dataset Throughput: ${THROUGHPUT} KB/s"
else
    echo "Throughput calculation: N/A"
fi

echo ""
echo "Latency Comparison:"
for i in "${!DATASETS[@]}"; do
    if [ "${RESULTS_FIRST_CHUNK_MS[$i]}" != "N/A" ]; then
        echo "  ${RESULTS_DATASET[$i]}: ${RESULTS_FIRST_CHUNK_MS[$i]} ms"
    fi
done

echo ""
echo "============================================================"
echo "Test Complete! ✅"
echo "============================================================"
echo ""
echo "Results saved. Ready for next test run."
echo ""
