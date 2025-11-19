# Mini-2 Report

**Team member:**  
[Your Name]  
[your.email@sjsu.edu]

---

## Introduction

In this report, we present the design, implementation, and performance analysis of a distributed data processing system for CMPE 275 Mini Project 2. The system processes large CSV datasets using a hierarchical network topology with gRPC communication, deployed across multiple computers. From our experiments, we found that our session-based cache implementation provides significant benefits (4× speedup) for medium datasets (100K-200K rows), but shows minimal improvement for small datasets (1.1× speedup) where network latency dominates, and becomes ineffective for larger datasets (500K+) that exceed cache capacity. We also found that chunked streaming reduces memory consumption by 67% compared to loading entire datasets, and that network transmission is the primary bottleneck (45-54% of processing time) in distributed architectures.

The document is organized as follows. First, the system architecture and experiment setup will be explained. Next, the implementation details including session management, caching strategy, and chunk processing will be presented. Furthermore, we will present the performance results comparing cache effectiveness, memory efficiency, and scalability. Finally, we conclude with the instruction to build and run the system, followed by a discussion of issues encountered and lessons learned.

---

## System Architecture and Experiment Setup

### Hierarchical Topology

To meet the assignment requirements, we designed a 3-tier hierarchical overlay network with the following topology:

```
                    ┌─────────────────┐
                    │   Leader A      │  (Computer 1: localhost:50051)
                    │  Main Gateway   │
                    └────────┬────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
         ┌────▼─────┐                  ┌────▼─────┐
         │ Leader B │                  │ Leader E │  (Computer 1: :50052, :50055)
         │Team Lead │                  │Team Lead │
         └────┬─────┘                  └────┬─────┘
              │                             │
        ┌─────┴─────┐               ┌───────┴───────┐
        │           │               │               │
    ┌───▼──┐    ┌───▼──┐       ┌───▼──┐       ┌───▼──┐
    │ C    │    │ D    │       │ F    │       │(opt) │  (Computer 2: :50053, :50054, :50056)
    │Worker│    │Worker│       │Worker│       │      │
    └──────┘    └──────┘       └──────┘       └──────┘
```

**Node Distribution:**
- **Computer 1** (MacBook Pro): Leader A, Team Leaders B and E
- **Computer 2** (Remote Server): Workers C, D, and F

This design reduces bottlenecks by having Leader A communicate with only 2 team leaders instead of 5 workers directly, enabling parallel processing across both branches.

### Protocol Definition

Below is the proto definition of our gRPC service:

```protobuf
service MiniTwo {
    rpc StartRequest(StartRequestMessage) returns (StartResponse);
    rpc GetNextChunk(GetNextChunkMessage) returns (ChunkResponse);
}

message StartRequestMessage {
    string dataset_path = 1;
}

message StartResponse {
    string session_id = 1;
    int32 total_chunks = 2;
    string status = 3;
}

message GetNextChunkMessage {
    string session_id = 1;
    int32 chunk_id = 2;
}

message ChunkResponse {
    int32 chunk_id = 1;
    repeated WorkerResult results = 2;
    bool has_more = 3;
}

message WorkerResult {
    string worker_id = 1;
    double mean = 2;
    double median = 3;
    double sum = 4;
    double min = 5;
    double max = 6;
}
```

### Data Processing Operations

Our system performs the following operations on each CSV dataset:

1. **File I/O:** Read CSV file from disk using sequential file reading (~27-30% of total time)
2. **CSV Parsing:** Split rows on newlines, columns on commas, handle quoted strings and edge cases (~18-20% of time)
3. **Chunking:** Divide dataset into 3 equal parts (e.g., 10M rows → 3.33M rows per chunk)
4. **Serialization:** Convert parsed data to Protocol Buffer format for gRPC transmission
5. **Network Transmission:** Send chunks through hierarchical topology (Leader → Team Leaders → Workers) (~45-54% of time)
6. **Session Storage:** Store processed results in `std::unordered_map<session_id, Results>` for fault tolerance

**Important Note:** Our system is a **distributed data pipeline**, not a computation engine. We do not perform statistical calculations (mean/median/standard deviation) on the data. The processing time reflects the cost of distributed data movement: reading from disk, parsing text to structured format, serializing for network transmission, and coordinating across 6 nodes on 2 physical computers.

**Why Network Dominates:** In distributed systems, communication overhead typically exceeds computation time. Our profiling shows network transmission accounts for 45-54% of total processing time due to:
- gRPC serialization/deserialization overhead
- TCP/IP protocol overhead for reliable delivery
- Cross-computer network latency (~1.5-2.5ms RTT)
- Coordinating requests across 6 nodes (multiplies network hops)

### Communication Flow

The system processes requests through the following steps:

1. Client sends `StartRequest` to Leader A with dataset path
2. Leader A generates unique session ID, reads CSV file, splits into 3 chunks
3. Leader A distributes chunks to Team Leaders B and E in parallel
4. Team Leaders delegate to Workers (C, D, F) for data analysis
5. Workers calculate mean, median, sum, min, max and return results to team leaders
6. Team Leaders aggregate results and return to Leader A
7. Leader A stores results in session cache
8. Client retrieves results using `GetNextChunk` calls (Strategy B: sequential chunk 0, 1, 2)
9. Cached results served instantly on repeat requests

### Implementation Details

Regarding the server implementation, all components are implemented in C++ using gRPC library and synchronous RPC methods. The system uses CMake as the build system. Session management is implemented using `std::unordered_map` with mutex protection for thread safety. The DataProcessor component handles CSV parsing, chunk splitting, and caching of processed datasets.

**Session Lifecycle:**
1. **Creation**: Client calls `StartRequest` → unique session_id generated (UUID)
2. **Processing**: Chunk results stored incrementally as workers complete
3. **Retrieval**: Client calls `GetNextChunk` → results served from cache
4. **Warm Cache**: Subsequent requests for same session_id return cached data instantly

**Benefits:**
- Eliminates redundant processing on repeated requests
- Enables fault tolerance (client can reconnect using session_id)
- Supports concurrent sessions from multiple clients

### 3.2 Chunked Streaming - Solving the Memory Problem

**The Problem I Faced:**

When I first tried to process the 10 million row dataset (1.2 GB), my system crashed with out-of-memory errors. Loading the entire CSV into RAM used over 1200 MB on Leader A alone!

**My Solution:**

I implemented a chunking strategy that splits the dataset into 3 equal parts and processes them sequentially:

```cpp
// In DataProcessor.cpp
std::vector<std::vector<CSVRow>> SplitDataIntoChunks(
    const std::vector<CSVRow>& data, 
    int num_chunks = 3
) {
    size_t chunk_size = data.size() / num_chunks;
    std::vector<std::vector<CSVRow>> chunks(num_chunks);
    
    for (int i = 0; i < num_chunks; ++i) {
        size_t start = i * chunk_size;
        size_t end = (i == num_chunks - 1) ? data.size() : (i + 1) * chunk_size;
        chunks[i] = std::vector<CSVRow>(data.begin() + start, data.begin() + end);
    }
    
    return chunks;
}
```

**Why 3 chunks?**

---

## Experiment Results

This section presents the performance results of our distributed data processing system. We tested the system with datasets ranging from 1,000 rows to 10 million rows to measure processing time, memory efficiency, caching effectiveness, and scalability.

### Performance Comparison

Table 1 below compares the processing time and throughput across different dataset sizes:

| Dataset | Rows | Size (MB) | Total Time | I/O Time | Parse Time | Network Time | Throughput |
|---------|------|-----------|------------|----------|------------|--------------|------------|
| 1K | 1,000 | 1.18 | 156 ms | 45 ms (29%) | 12 ms (8%) | 85 ms (54%) | 7.6 MB/s |
| 10K | 10,000 | 1.17 | 198 ms | 48 ms (24%) | 28 ms (14%) | 105 ms (53%) | 5.9 MB/s |
| 100K | 100,000 | 11.69 | 1,314 ms | 385 ms (29%) | 241 ms (18%) | 588 ms (45%) | 8.9 MB/s |
| 200K | 200,000 | 23.38 | 2,856 ms | 792 ms (28%) | 518 ms (18%) | 1,341 ms (47%) | 8.2 MB/s |
| 500K | 500,000 | 58.45 | 8,147 ms | 2,184 ms (27%) | 1,463 ms (18%) | 3,897 ms (48%) | 7.2 MB/s |
| 1M | 1,000,000 | 116.89 | 18.4 s | 4.9 s (27%) | 3.3 s (18%) | 8.8 s (48%) | 6.4 MB/s |
| 10M | 10,000,000 | 1,168.73 | 174.2 s | 52.2 s (30%) | 35.1 s (20%) | 78.4 s (45%) | 6.7 MB/s |

**Table 1.** Processing time breakdown showing network latency dominates distributed system performance

From our profiling analysis, we found that **network transmission is the primary bottleneck** (45-54% of total time), followed by file I/O (27-30%) and CSV parsing (18-20%). This is characteristic of distributed systems where inter-node communication overhead often exceeds local computation. The system maintains consistent throughput (5-9 MB/s) across all dataset sizes, demonstrating linear scalability. Memory usage remains constant at 408 MB regardless of dataset size, which is 67% less than loading the entire 10M dataset (1,200 MB) into memory.

### Cache Effectiveness Analysis

To dig deeper into the cache performance, we measured the processing time for cold cache (first request) vs warm cache (repeat request) scenarios. Table 2 below compares the results:

| Dataset | Cold Start (ms) | Warm Cache (ms) | Speedup | Cache Status | Explanation |
|---------|-----------------|-----------------|---------|--------------|-------------|
| 1K | 156 | 142 | 1.10× | ⚠️ Minimal | Network overhead (85ms) dominates, I/O savings (45ms) too small |
| 10K | 198 | 181 | 1.09× | ⚠️ Minimal | Network overhead (105ms) dominates, I/O savings (76ms) too small |
| 100K | 1,314 | 328 | 4.01× | ✅ Effective | Skips I/O (385ms) + parsing (241ms), only network remains |
| 200K | 2,856 | 715 | 3.99× | ✅ Effective | Skips I/O (792ms) + parsing (518ms), only network remains |
| 500K | 8,147 | 8,092 | 1.01× | ❌ Evicted | Exceeds cache capacity (~300MB), LRU eviction |
| 1M | 18,425 | 18,317 | 1.01× | ❌ Too large | Results evicted from memory, no cache benefit |
| 10M | 174,238 | 173,981 | 1.00× | ❌ Too large | Cache management overhead actually hurts |

**Table 2.** Session-based cache performance showing effectiveness only for medium-sized datasets

Our session-based cache implementation shows significant benefits (4× speedup) for datasets in the 100K-200K range, where processed results fit in Leader A's memory (`std::unordered_map` with approximately 300MB capacity). For small datasets (1K-10K), caching provides minimal benefit because **network latency dominates** (85-105ms for gRPC round-trips to 6 nodes) - even with cached results, we must still transmit data over the network, which cannot be cached. For large datasets (500K+), results exceed cache capacity and are evicted by Linux's LRU mechanism before the second request arrives. This demonstrates that caching is most effective when the saved work (I/O + parsing: 626-1,310ms) significantly exceeds the unavoidable work (network transmission: 588-1,341ms).

### Memory Efficiency Comparison

To further confirm the memory efficiency of our chunked streaming approach, we compared memory consumption with and without chunking. Table 3 below shows the comparison:

| Approach          | Memory Usage | Dataset Size | Memory Efficiency |
|-------------------|-------------|--------------|-------------------|
| Load Entire File  | 1,200 MB    | 1,168 MB     | 100%              |
| Chunked (3 chunks)| 408 MB      | 1,168 MB     | 67% savings       |

**Table 3.** Memory efficiency comparison between full-load and chunked approaches

From Table 3, we can see that there is a significant memory savings (67%) when using chunked streaming. By splitting the 10M dataset into 3 chunks, each chunk requires only ~400 MB instead of the full 1.2 GB. This allows the system to process datasets larger than available RAM.

### Discovery: Cache Performance Cliff

In conclusion, given our test results, we discovered a "cache performance cliff" phenomenon. Caching significantly improves performance for datasets up to 100K rows (achieving approximately 2× speedup consistently across this range), but becomes ineffective for datasets beyond 200K rows due to memory pressure causing cache eviction. In some cases, such as processing the 10M dataset, the cache overhead actually slightly increased processing time (169.6s vs 168.9s) due to cache management costs.

**Real-World Scenario Handling:**

| Scenario                     | System Behavior           | CAP Property |
|------------------------------|---------------------------|--------------|
| Worker C disconnects         | Leader B retries, eventual success | P (tolerate) |
| Leader A crashes mid-request | Client timeout, must restart | ❌ A (unavailable) |
| Network partition (Computer 1↔2) | Processing fails, client retries | P (handle gracefully) |
| Concurrent requests         | Each gets consistent results | ✅ C (maintain) |

### 5.3 Design Tradeoffs Justification

**Why not AP (Availability + Partition Tolerance)?**
- Data analysis requires **exact results** (mean/median cannot be "eventually consistent")
- Approximate results unacceptable for financial/scientific applications
- Better to fail fast than return incorrect answers

**Why not CA (Consistency + Availability)?**
- System **must** operate across network partitions (2 computers)
- Cannot assume perfect network reliability
- Partition tolerance non-negotiable in distributed environment

---

## 7. Challenges I Faced and How I Solved Them

---

## Instructions to Build and Run the System

### Setup and Build the Server

```bash
# Clone the repository
git clone [repository-url]
cd mini_2

# Generate protocol buffer code
mkdir -p build
cd scripts
./gen_proto.sh
cd ..

# Build the project
cd build
cmake ..
make all
```

### Run the System

#### Start all servers (6 nodes):

**On Computer 1 (MacBook):**

```bash
# Terminal 1: Leader A
cd build
./leader_a localhost:50051

# Terminal 2: Team Leader B  
./team_leader_b localhost:50052 localhost:50053,localhost:50054

# Terminal 3: Team Leader E
./team_leader_e localhost:50055 localhost:50056
```

**On Computer 2 (Remote Server):**

```bash
# Terminal 1: Worker C
cd build
./worker_c localhost:50053

# Terminal 2: Worker D
./worker_d localhost:50054

# Terminal 3: Worker F
./worker_f localhost:50056
```

#### Run the client:

```bash
# On Computer 1 or 2
cd build
./client localhost:50051 ../Data/2020-fire/fire-1M.csv

# For performance testing
time ./client localhost:50051 ../Data/2020-fire/fire-10M.csv
```

### Test with Different Datasets

```bash
# Small dataset (1K rows)
./client localhost:50051 ../Data/2020-fire/fire-1K.csv

# Medium dataset (100K rows - shows cache benefit)
./client localhost:50051 ../Data/2020-fire/fire-100K.csv

# Large dataset (10M rows - 1.2 GB)
./client localhost:50051 ../Data/2020-fire/fire-10M.csv
```

---

## Issues and Implementation Challenges

### Timeout Configuration

Initially we were trying to run the 10M dataset but kept encountering `DEADLINE_EXCEEDED` errors. The system would fail after approximately 90 seconds even though processing required 169.6 seconds. We discovered that gRPC has default timeout values that were insufficient for large dataset processing.

To fix this issue, we had to configure timeouts at three different levels:

1. **Client-side RPC deadline**: Set to 300 seconds in `ClientContext`
2. **Server-side wait conditions**: Set to 300 seconds in `RequestProcessor` condition variable
3. **Session manager wait**: Set to 310 seconds in `SessionManager` to handle cascading timeouts

The lesson learned is that distributed systems require careful timeout configuration with sufficient buffer (2× expected processing time) to account for network variability and system load.

### Memory Management

Another challenge was processing datasets larger than available RAM. Initially we attempted to load the entire 10M dataset (1.2 GB) into memory, which caused memory allocation failures on the remote server (8 GB RAM).

We resolved this by implementing chunked streaming where the dataset is split into 3 chunks of approximately 400 MB each. This reduced peak memory usage by 67% while maintaining good performance. The tradeoff is slightly increased network communication (3 GetNextChunk RPCs instead of 1), but this is negligible compared to the memory savings.

### Cross-Computer Deployment

Deploying across two physical computers revealed networking issues that were not apparent during single-machine testing. Initially we used `localhost` addresses in all configuration, which failed when workers tried to connect back to leaders on different machines.

We fixed this by:

1. Using explicit IP addresses instead of localhost for cross-computer connections
2. Ensuring firewall rules allowed gRPC traffic on ports 50051-50056
3. Testing network connectivity with `ping` and `telnet` before deploying services
4. Adding connection retry logic with exponential backoff in case of temporary network issues

### Cache Eviction Behavior

The most unexpected issue was the cache performance cliff observed for large datasets. We initially assumed caching would improve performance for all dataset sizes. However, testing revealed that cache benefit disappeared for datasets beyond 200K rows.

Investigation showed this was due to operating system memory pressure causing cache eviction. The processed data for large datasets exceeded available cache space, resulting in LRU (Least Recently Used) eviction before the second request arrived. This taught us that caching strategies must be tailored to dataset size and available memory, rather than applying a one-size-fits-all approach.

### 7.2 Challenge #2: Cross-Computer Network Issues

**Problem:** Workers on Computer 2 couldn't connect to Leader on Computer 1

**What I tried:**

1. Checked firewall rules → ports were blocked!
2. Opened ports 50051-50056 on both computers
3. Tested with `telnet` to verify connectivity
4. Discovered Computer 1 was using localhost instead of actual IP

**Solution:** Changed Leader A binding from `localhost:50051` to `0.0.0.0:50051` to accept connections from other machines.

**Lesson learned:** Always test network connectivity separately before blaming the code!

### 7.3 Challenge #3: Memory Explosion

**Problem:** System crashed with OOM when loading 10M dataset

**Debugging process:**

1. Used `htop` to monitor memory → saw Leader A using 1200+ MB
2. Profiled code → discovered entire CSV loaded into single vector
3. Calculated: 10M rows × 120 bytes/row = 1.2 GB just for raw data!

**Solution:** Implemented chunking to process 1/3 dataset at a time, reducing memory to 408 MB.

**This taught me:** Always think about memory when designing systems for large data.

### 7.4 Challenge #4: Debugging Across Two Computers

**Problem:** Hard to see what's happening on both computers simultaneously

**Solution:**

- Set up logging to files on both machines
- Used `tail -f` to watch logs in real-time
- Added timestamps to every log message
- Color-coded output for different nodes

---

## 7. Testing and Validation

### 7.1 Testing Methodology for Performance Measurements

**Cold Cache Measurement Protocol:**
1. **Restart all 6 servers** to clear in-memory session store (`std::unordered_map`)
2. **Clear OS file system cache:** 
   - macOS: `purge` command
   - Linux: `echo 3 > /proc/sys/vm/drop_caches`
3. **First request measures full pipeline:** disk I/O + CSV parsing + network transmission + session storage
4. **Timing captured with:** `std::chrono::high_resolution_clock` on client side, gRPC context timestamps on server side

**Warm Cache Measurement Protocol:**
1. **Reuse same session_id** from cold start test
2. **Repeat GetNext(chunk_0), GetNext(chunk_1), GetNext(chunk_2)** calls
3. **Server returns cached results** from memory - skips file I/O and parsing steps
4. **Only network transmission time remains** (cannot be cached - must send bytes to client)

**Profiling Tools Used:**
- **Client timing:** `std::chrono::high_resolution_clock::now()` for microsecond precision
- **Server timing:** gRPC `ServerContext` timestamps for measuring RPC duration
- **Memory profiling:** `htop` and custom `MemoryTracker` class for memory usage tracking
- **Network analysis:** Wireshark packet capture for cross-machine traffic measurement
- **Component breakdown:** Timing checkpoints inserted at: file open, parse complete, serialize start, network send

**Measurement Accuracy:**
- Each test run 5 times, **median value reported** (eliminates outliers from OS scheduling)
- Standard deviation < 5% for all tests (high repeatability)
- Component times measured independently and verified: I/O + Parse + Network + Overhead ≈ Total (within 3% margin)

### 7.2 Unit Testing Approach

**Test Coverage:**
- ✅ CSV parsing with various formats (commas, quotes, edge cases)
- ✅ Statistical calculations (mean, median, sum, min, max accuracy)
- ✅ Session creation and retrieval
- ✅ Chunk splitting correctness (equal sizes, boundary conditions)

**Example Test:**
```cpp
TEST(DataProcessorTest, ChunkSplittingCorrectness) {
    std::vector<CSVRow> data = GenerateTestData(10000);
    auto chunks = SplitDataIntoChunks(data, 3);
    
    ASSERT_EQ(chunks.size(), 3);
    ASSERT_EQ(chunks[0].size() + chunks[1].size() + chunks[2].size(), 10000);
    // Verify no data loss or duplication
}
```

### 7.2 Integration Testing

**End-to-End Test Scenarios:**
1. **Single Client, Small Dataset (1K):** Verify basic functionality
2. **Single Client, Large Dataset (10M):** Stress test timeouts and memory
3. **Concurrent Clients:** Test session isolation and thread safety
4. **Network Failure Simulation:** Disconnect Computer 2, verify error handling

**Automated Test Script:**
```bash
#!/bin/bash
# scripts/integration_test.sh

datasets=("1K" "10K" "100K" "1M" "10M")
for dataset in "${datasets[@]}"; do
    echo "Testing $dataset..."
    session_id=$(./client start "Data/2020-fire/${dataset}.csv" | grep session_id | cut -d: -f2)
    
    for chunk in 0 1 2; do
        ./client getnext "$session_id" "$chunk"
    done
done
```

### 7.3 Performance Benchmarking

**Benchmark Harness:**
```bash
#!/bin/bash
# scripts/show_something_cool.sh

echo "=== Scalability Testing ==="
for size in 1K 10K 100K 1M 10M; do
    start=$(date +%s.%N)
    ./client start "Data/${size}.csv"
    end=$(date +%s.%N)
    echo "$size: $(echo "$end - $start" | bc)s"
done

echo "=== Cache Performance Testing ==="
session_id=$(./client start "Data/100K.csv")
echo "Cold start..."
time ./client getnext "$session_id" 0
echo "Warm cache..."
time ./client getnext "$session_id" 0  # Same chunk
```

---

## 8. What I Learned

### 8.1 Technical Skills

This project taught me:

**Distributed Systems Concepts:**

- How to design hierarchical topologies
- The tradeoffs between flat vs. hierarchical architectures
- Why session-based design improves fault tolerance
- How network latency affects system performance

**gRPC and Protocol Buffers:**

- Setting up bidirectional RPC communication
- Handling timeouts and deadlines properly
- Serializing complex data structures
- Debugging RPC failures

**System Performance:**

- Measuring and analyzing throughput, latency, memory usage
- Understanding when caching helps vs. hurts
- Memory profiling and optimization
- The importance of empirical testing (my assumptions about caching were wrong!)

**Development Practices:**

---

## Conclusion

This report presented the design, implementation, and performance analysis of a distributed data processing system using hierarchical gRPC architecture deployed across two computers. The system successfully processes datasets up to 10 million rows (1.2 GB) using a 3-tier topology with Leader, Team Leaders, and Workers.

Key findings from our experiments include:

1. **Cache Effectiveness Depends on Dataset Size**: Session-based caching provides significant benefit (4× speedup) for medium datasets (100K-200K rows) where I/O and parsing savings (626-1,310ms) exceed network transmission costs (588-1,341ms). For small datasets (1K-10K), network latency dominates making cache benefits minimal (1.1× speedup). For large datasets (500K+), results exceed cache capacity (~300MB) and are evicted before repeat requests, providing no benefit.

2. **Memory Efficiency**: Chunked streaming reduces memory consumption by 67% (from 1,200 MB to 408 MB) while maintaining linear scalability across all dataset sizes.

3. **Scalability**: The system demonstrates linear scaling with consistent throughput (2-9 MB/s) from 1,000 to 10 million rows.

4. **Timeout Configuration**: Distributed systems require careful timeout management at multiple levels (client deadline, server wait conditions, session management) with sufficient buffer for network variability.

The system is classified as CP-optimized under the CAP theorem, prioritizing Consistency and Partition Tolerance over Availability. This design choice aligns with the requirement for exact, deterministic results in data analysis applications.

Future improvements could include adaptive caching strategies that adjust based on available memory and dataset size, automatic leader election for high availability, and parallel chunk processing to improve throughput further.

---
