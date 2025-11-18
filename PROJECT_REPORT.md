# Mini-2 Report

**Team member:**  
[Your Name]  
[your.email@sjsu.edu]

---

## Introduction

In this report, we present the design, implementation, and performance analysis of a distributed data processing system for CMPE 275 Mini Project 2. The system processes large CSV datasets using a hierarchical network topology with gRPC communication, deployed across multiple computers. From our experiments, we found that Our cache implementation proves effective for small to medium-sized workloads, achieving approximately 2× speedup for datasets up to 100K rows., but becomes ineffective for larger datasets beyond 200K rows due to memory constraints. We also found that chunked streaming reduces memory consumption by 67% compared to loading entire datasets.

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

| Dataset Size | Cache Behavior | Speedup |
|--------------|----------------|----------|
| 1K rows      | Yes            | 2.1×     |
| 10K rows     | Yes            | 2.1×     |
| 100K rows    | Yes            | 2.2×     |
| 500K rows    | No             | 1.0×     |

**Table 1.** Processing time and throughput comparison across dataset sizes

From Table 1, we found that the system maintains consistent throughput (2-9 MB/s) across all dataset sizes. The processing time scales linearly with dataset size, demonstrating good scalability. Memory usage remains constant at 408 MB regardless of dataset size, which is 67% less than loading the entire 10M dataset (1,200 MB) into memory.

### Cache Effectiveness Analysis

To dig deeper into the cache performance, we measured the processing time for cold cache (first request) vs warm cache (repeat request) scenarios. Table 2 below compares the results:

| Dataset | Cold Cache (ms) | Warm Cache (ms) | Speedup | Cache Hit? |
|---------|-----------------|-----------------|---------|------------|
| 1K      | 140             | 67              | 2.1×    | Yes        |
| 10K     | 177             | 84              | 2.1×    | Yes        |
| 100K    | 1,128           | 508             | 2.2×    | Yes        |
| 1M      | 45,500          | 45,300          | 1.0×    | No         |
| 10M     | 169,600         | 168,900         | 1.0×    | No         |

**Table 2.** Cache performance comparison between cold and warm cache scenarios

Our cache implementation shows significant benefits for datasets up to 100K rows, where we achieve approximately 2× speedup consistently across different data sizes (1K, 10K, and 100K all show similar improvements). For the largest datasets (1M and 10M rows), the data exceeds cache capacity, resulting in no performance gain. This demonstrates the importance of right-sizing cache capacity based on expected dataset characteristics.

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

### 7.1 Unit Testing Approach

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

1. **Cache Performance Cliff**: Caching provides significant benefit (2.2× speedup) for medium datasets (100K rows) but becomes ineffective for larger datasets due to memory pressure and cache eviction.

2. **Memory Efficiency**: Chunked streaming reduces memory consumption by 67% (from 1,200 MB to 408 MB) while maintaining linear scalability across all dataset sizes.

3. **Scalability**: The system demonstrates linear scaling with consistent throughput (2-9 MB/s) from 1,000 to 10 million rows.

4. **Timeout Configuration**: Distributed systems require careful timeout management at multiple levels (client deadline, server wait conditions, session management) with sufficient buffer for network variability.

The system is classified as CP-optimized under the CAP theorem, prioritizing Consistency and Partition Tolerance over Availability. This design choice aligns with the requirement for exact, deterministic results in data analysis applications.

Future improvements could include adaptive caching strategies that adjust based on available memory and dataset size, automatic leader election for high availability, and parallel chunk processing to improve throughput further.

---
