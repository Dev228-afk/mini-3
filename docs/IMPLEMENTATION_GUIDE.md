# Phase-by-Phase Implementation Guide

This note describes how we actually wired things up for each phase.

---

## Phase 1: Basecamp - Establishing the Foundation

### Goals
✓ Create working gRPC communication infrastructure (multiple physical computers are connected in the network)  
✓ Deploy across multiple machines  
✓ Establish baseline performance metrics  

### Step 1.1: Verify Configuration System

**What we did:**

1. Looked at `config/network_setup.json` to understand the network topology
2. Used the configuration parser in `src/cpp/common/config.cpp`
3. Made sure each process reads its identity from the config instead of hardcoding

**Quick check:**
```bash
# Build the project
cd scripts
./build.sh

# Test configuration loading
./build/mini2_server --config config/network_setup.json --node A
```

**What we watched for:**

- Process starts and prints the right node id
- No obvious hardcoded host/port values
- Misconfigured files print a clear error and exit

### Step 1.2: Implement Basic gRPC Services

**Current Services (from protos/minitwo.proto):**
- `NodeControl`: Heartbeat/Ping for connectivity
- `TeamIngress`: Request handling between internal nodes
- `ClientGateway`: Client-facing interface (A only)

**What we did:**

1. Reviewed `src/cpp/server/Handlers.cpp`
2. Fixed the small typo (`False` → `false`)
3. Added simple logs to each RPC handler
4. Returned basic error codes when something goes wrong

**Example tiny change:**
```cpp
// In Handlers.cpp, update PollNext:
Status PollNext(ServerContext*, const mini2::PollReq*, mini2::PollResp* resp) override {
    resp->set_ready(false);  // Fix: lowercase 'false'
    resp->set_has_more(false); 
    return Status::OK;
}
```

### Step 1.3: Test Connectivity

**Tasks:**
1. Start all 6 servers on localhost first
2. Use the client to test Ping on each connection
3. Verify overlay topology enforcement

**Commands:**
```bash
# Terminal 1-6: Start each server
./build/mini2_server --config config/network_setup.json --node A
./build/mini2_server --config config/network_setup.json --node B
# ... etc for C, D, E, F

# Terminal 7: Test client connectivity
./build/mini2_client --gateway localhost:50050 --mode baseline
```

**Testing Matrix:**
| From | To | Should Connect? | Test Result |
|------|-----|----------------|-------------|
| A | B | Yes | |
| A | C | No | |
| A | E | Yes | |
| B | C | Yes | |
| B | D | Yes | |
| E | D | Yes | |
| E | F | Yes | |

### Step 1.4: Measure Baseline Performance

**Metrics to Collect:**
1. **RTT (Round-Trip Time)**: Measure Ping latency
2. **CPU Usage**: Use `top` or `ps` to monitor each process
3. **Memory Usage**: Track RSS (Resident Set Size)

**Measurement Code:**
```cpp
// Add to ClientMain.cpp for RTT measurement
auto start = std::chrono::high_resolution_clock::now();
auto status = stub->Ping(&ctx, heartbeat, &ack);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
std::cout << "RTT: " << duration.count() / 1000.0 << " ms" << std::endl;
```

**Record results in:** `results/phase1_baseline.csv`

### Step 1.5: Deploy to Multiple Machines

**Two-Computer Setup:**
- Host1: {A, B, D}
- Host2: {C, E, F}

**Tasks:**
1. Update `config/network_setup.json` with actual hostnames
2. Build the project on both machines
3. Start servers on appropriate hosts
4. Test cross-host communication

**Configuration Update:**
```json
{
  "nodes": [
    {"id": "A", "role": "LEADER", "host": "192.168.1.100", "port": 50050, "team": "GREEN"},
    {"id": "B", "role": "TEAM_LEADER", "host": "192.168.1.100", "port": 50051, "team": "GREEN"},
    // ... update other hosts
  ]
}
```

---

## Phase 2: Request Forwarding and Data Aggregation

### Goals
✓ Implement full request-response path  
✓ Enable team-based work distribution  
✓ Measure end-to-end performance  

### Step 2.1: Implement Request Forwarding

**Current Flow:**
```
Client → A (ClientGateway::RequestOnce)
A → Team Leaders (TeamIngress::HandleRequest)
Team Leaders → Workers (TeamIngress::HandleRequest)
```

**Tasks:**
1. Process A needs to:
   - Parse incoming request
   - Determine which team(s) to contact (Green, Pink, or both)
   - Forward request to appropriate team leaders
   
2. Create gRPC client stubs in server processes for peer communication

**Implementation Hint:**
```cpp
// In ServerMain.cpp or a new RequestRouter class
class RequestRouter {
    std::map<std::string, std::unique_ptr<mini2::TeamIngress::Stub>> peer_stubs;
    
public:
    void InitializePeers(const NetworkConfig& cfg, const std::string& my_id) {
        // Create stubs for nodes we can communicate with
        for (const auto& edge : cfg.overlay.edges) {
            if (edge.first == my_id) {
                std::string peer_id = edge.second;
                auto& peer_info = cfg.nodes.at(peer_id);
                std::string addr = peer_info.host + ":" + std::to_string(peer_info.port);
                auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
                peer_stubs[peer_id] = mini2::TeamIngress::NewStub(channel);
            }
        }
    }
    
    void ForwardRequest(const mini2::Request* req) {
        // Forward to appropriate team leaders based on need_green/need_pink
    }
};
```

### Step 2.2: Implement Worker Result Collection

**Current Flow:**
```
Workers → Team Leaders (TeamIngress::PushWorkerResult)
Team Leaders → A (TeamIngress::PushWorkerResult)
```

**Tasks:**
1. Workers generate mock results (use realistic data structures!)
2. Team leaders aggregate results from their team
3. Process A collects all team results
4. Process A combines and sends to client

**Data Structure Example:**
```cpp
// In worker code - use realistic types!
mini2::WorkerResult result;
result.set_request_id(req->request_id());
result.set_part_index(0);

// Example: Serialize a structured result
struct DataRow {
    int id;
    double value;
    bool is_valid;
    std::string label;
};

std::vector<DataRow> rows = ProcessData(); // Your actual work
// Serialize rows to bytes (use protobuf, or manual serialization)
std::string serialized = SerializeRows(rows);
result.set_payload(serialized);
```

### Step 2.3: Complete End-to-End Test

**Test Scenarios:**
1. Request that needs only Green team
2. Request that needs only Pink team
3. Request that needs both teams

**Client Test Code:**
```cpp
// Update ClientMain.cpp
mini2::Request req;
req.set_request_id("test-001");
req.set_query("SELECT * FROM data");
req.set_need_green(true);
req.set_need_pink(false);

mini2::AggregatedResult result;
grpc::ClientContext ctx;
auto status = stub->RequestOnce(&ctx, req, &result);

if (status.ok()) {
    std::cout << "Total rows: " << result.total_rows() << std::endl;
    std::cout << "Total bytes: " << result.total_bytes() << std::endl;
}
```

### Step 2.4: Measure Performance

**Metrics:**
- End-to-end latency for different request sizes
- Memory usage at Process A
- Network message count

**Record results in:** `results/phase2_aggregation.csv`

---

## Phase 3: Multi-Chunk Response Strategies

### Goals
✓ Implement Strategy A: Client-controlled paging  
✓ Implement Strategy B: Server-managed sessions  
✓ Compare performance and fairness  

### Step 3.1: Strategy A - Client-Controlled Paging

**Design:**
- Client requests data once, A stores the complete result
- Client requests chunks by index: 0, 1, 2, ...
- A returns one chunk at a time from stored result

**Implementation:**
```cpp
// In ClientGatewayService
class ClientGatewayService final : public mini2::ClientGateway::Service {
    std::mutex results_mutex;
    std::map<std::string, mini2::AggregatedResult> stored_results;
    
public:
    Status RequestOnce(ServerContext*, const mini2::Request* req, 
                       mini2::AggregatedResult* out) override {
        // Collect full result
        auto complete_result = GatherAllData(req);
        
        // Store it
        {
            std::lock_guard<std::mutex> lock(results_mutex);
            stored_results[req->request_id()] = complete_result;
        }
        
        *out = complete_result;
        return Status::OK;
    }
    
    Status GetNext(ServerContext*, const mini2::NextChunkReq* req,
                   mini2::NextChunkResp* resp) override {
        std::lock_guard<std::mutex> lock(results_mutex);
        auto it = stored_results.find(req->request_id());
        if (it == stored_results.end()) {
            return Status(grpc::StatusCode::NOT_FOUND, "Session not found");
        }
        
        const auto& result = it->second;
        if (req->next_index() < result.chunks_size()) {
            resp->set_chunk(result.chunks(req->next_index()));
            resp->set_has_more(req->next_index() + 1 < result.chunks_size());
        } else {
            resp->set_has_more(false);
        }
        
        return Status::OK;
    }
};
```

**Client Code:**
```cpp
// Request all data first
mini2::AggregatedResult full_result;
stub->RequestOnce(&ctx, req, &full_result);

// Now request chunks
for (uint32_t i = 0; i < full_result.chunks_size(); i++) {
    mini2::NextChunkReq chunk_req;
    chunk_req.set_request_id(req.request_id());
    chunk_req.set_next_index(i);
    
    mini2::NextChunkResp chunk_resp;
    grpc::ClientContext chunk_ctx;
    stub->GetNext(&chunk_ctx, chunk_req, &chunk_resp);
    
    ProcessChunk(chunk_resp.chunk());
}
```

### Step 3.2: Strategy B - Server-Managed Sessions

**Design:**
- Client initiates request, gets session ID immediately
- A begins gathering data in background
- Client polls with session ID
- A returns chunks as they become available
- A does NOT store complete result

**Implementation:**
```cpp
// Session management
class SessionManager {
    struct Session {
        std::string request_id;
        std::queue<std::string> ready_chunks;
        bool complete = false;
        std::mutex mtx;
    };
    
    std::map<std::string, std::shared_ptr<Session>> sessions;
    std::mutex sessions_mutex;
    
public:
    std::string CreateSession(const mini2::Request* req) {
        auto session = std::make_shared<Session>();
        session->request_id = req->request_id();
        
        {
            std::lock_guard<std::mutex> lock(sessions_mutex);
            sessions[req->request_id()] = session;
        }
        
        // Start background thread to gather data
        std::thread([this, session, req]() {
            GatherDataIncrementally(session, req);
        }).detach();
        
        return req->request_id();
    }
    
    bool PollForChunk(const std::string& request_id, std::string& chunk_out, bool& has_more) {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        auto it = sessions.find(request_id);
        if (it == sessions.end()) return false;
        
        auto session = it->second;
        std::lock_guard<std::mutex> session_lock(session->mtx);
        
        if (session->ready_chunks.empty()) {
            has_more = !session->complete;
            return false;
        }
        
        chunk_out = session->ready_chunks.front();
        session->ready_chunks.pop();
        has_more = !session->ready_chunks.empty() || !session->complete;
        return true;
    }
};
```

### Step 3.3: Fairness Testing

**Test Setup:**
- Client 1: Request large dataset (many chunks)
- Client 2: Request small dataset (few chunks)
- Start both simultaneously
- Measure: Does Client 2 get blocked by Client 1?

**Test Code:**
```cpp
// Run two clients in parallel
auto client1_thread = std::thread([]() {
    // Large request
    RequestLargeDataset();
});

auto client2_thread = std::thread([]() {
    // Small request - should complete quickly
    RequestSmallDataset();
});

client1_thread.join();
client2_thread.join();
```

### Step 3.4: Compare Strategies

**Metrics to Compare:**
1. Memory usage at Process A
2. Time to first chunk
3. Total time to complete
4. Fairness (does small request get blocked?)
5. Number of RPC calls

**Record results in:** `results/phase3_comparison.csv`

**Format:**
```csv
strategy,result_size,memory_mb,time_to_first_ms,total_time_ms,rpc_calls,fair
A,small,50,100,500,10,yes
A,large,500,120,5000,100,no
B,small,25,50,450,15,yes
B,large,100,80,4800,150,yes
```

---

## Phase 4: Advanced Coordination with Shared Memory

### Goals
✓ Use shared memory for process status  
✓ Implement load-aware routing  
✓ Measure improvements  

### Step 4.1: Implement Shared Memory Segments

**Design:**
- Segment 1: Processes {A, B, D} on Host1
- Segment 2: Processes {C, E, F} on Host2
- Store: Process state, queue size, timestamp
- Do NOT store: Requests or results

**POSIX Shared Memory Example:**
```cpp
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct ProcessStatus {
    char process_id[8];
    enum State { IDLE, BUSY } state;
    uint32_t queue_size;
    int64_t last_update_ms;
};

struct SharedSegment {
    ProcessStatus processes[3]; // Max 3 processes per segment
    uint32_t count;
};

class SharedMemoryCoordinator {
    int shm_fd;
    SharedSegment* segment;
    
public:
    bool Initialize(const std::string& segment_name) {
        shm_fd = shm_open(segment_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) return false;
        
        ftruncate(shm_fd, sizeof(SharedSegment));
        segment = static_cast<SharedSegment*>(
            mmap(0, sizeof(SharedSegment), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)
        );
        
        return segment != MAP_FAILED;
    }
    
    void UpdateStatus(const std::string& id, ProcessStatus::State state, uint32_t queue_size) {
        // Find or add this process
        for (uint32_t i = 0; i < segment->count; i++) {
            if (std::string(segment->processes[i].process_id) == id) {
                segment->processes[i].state = state;
                segment->processes[i].queue_size = queue_size;
                segment->processes[i].last_update_ms = GetCurrentTimeMs();
                return;
            }
        }
        // Add new
        if (segment->count < 3) {
            auto& p = segment->processes[segment->count++];
            strncpy(p.process_id, id.c_str(), 7);
            p.state = state;
            p.queue_size = queue_size;
            p.last_update_ms = GetCurrentTimeMs();
        }
    }
    
    ProcessStatus GetStatus(const std::string& id) {
        for (uint32_t i = 0; i < segment->count; i++) {
            if (std::string(segment->processes[i].process_id) == id) {
                return segment->processes[i];
            }
        }
        return ProcessStatus{};
    }
    
    ~SharedMemoryCoordinator() {
        if (segment) munmap(segment, sizeof(SharedSegment));
        if (shm_fd != -1) close(shm_fd);
    }
};
```

### Step 4.2: Implement Load-Aware Routing

**Logic:**
```cpp
class LoadAwareRouter {
    SharedMemoryCoordinator shm_coord;
    
public:
    std::string SelectTeamLeader(const std::string& team) {
        std::vector<std::string> candidates;
        if (team == "GREEN") candidates = {"B"};
        else if (team == "PINK") candidates = {"E"};
        
        // Check shared memory for status
        std::string best_choice;
        uint32_t min_queue_size = UINT32_MAX;
        
        for (const auto& candidate : candidates) {
            auto status = shm_coord.GetStatus(candidate);
            if (status.state == ProcessStatus::IDLE) {
                return candidate; // Idle is best
            }
            if (status.queue_size < min_queue_size) {
                min_queue_size = status.queue_size;
                best_choice = candidate;
            }
        }
        
        return best_choice;
    }
};
```

### Step 4.3: Measure Improvements

**Comparison:**
- Run system WITHOUT shared memory coordination
- Run system WITH shared memory coordination
- Compare throughput, latency, fairness

**Test Scenario:**
- Multiple concurrent clients
- Mixed workload (some requests to Green, some to Pink)
- Measure: How often does the system choose busy servers?

**Record results in:** `results/phase4_shm_comparison.csv`

---

## Common Pitfalls to Avoid

### 1. Hardcoding
❌ **DON'T:**
```cpp
if (node_id == "A") { /* leader logic */ }
```

✅ **DO:**
```cpp
if (node_info.role == "LEADER") { /* leader logic */ }
```

### 2. Using gRPC Async APIs
❌ **DON'T:** Use `AsyncService`, `CompletionQueue`
✅ **DO:** Use synchronous `Service` with threads for concurrency

### 3. Storing Results in Shared Memory
❌ **DON'T:** Put request/response data in shared memory
✅ **DO:** Only use shared memory for coordination (status, flags)

### 4. String-Only Data Structures
❌ **DON'T:**
```cpp
result.set_value("123.45"); // Everything as string
```

✅ **DO:**
```cpp
MyData data;
data.set_int_field(123);
data.set_double_field(45.67);
data.set_bool_field(true);
```

### 5. Flat Network Topology
❌ **DON'T:** Make all nodes connect directly to A
✅ **DO:** Use the specified overlay (tree structure)

---

## Testing Checklist

### Phase 1: Basecamp
- [ ] All 6 processes start successfully
- [ ] Configuration loaded correctly (no hardcoding)
- [ ] Ping works on all overlay edges
- [ ] Ping fails on non-overlay edges
- [ ] Cross-host deployment works
- [ ] Baseline metrics collected

### Phase 2: Forwarding
- [ ] Requests route correctly to teams
- [ ] Workers generate results
- [ ] Results aggregate at team leaders
- [ ] Results aggregate at Process A
- [ ] Client receives complete response
- [ ] Performance measured

### Phase 3: Chunking
- [ ] Strategy A: Client paging works
- [ ] Strategy B: Server sessions work
- [ ] Fairness test conducted
- [ ] Memory measurements taken
- [ ] Comparison table completed

### Phase 4: Shared Memory
- [ ] Shared memory segments created
- [ ] Status updates working
- [ ] Load-aware routing implemented
- [ ] Improvements measured
- [ ] Comparison completed

---

## Next Steps

1. **Fix the code bug**: In `Handlers.cpp` line 39, change `False` to `false`
2. **Generate protocol buffers**: Run `scripts/gen_proto.sh`
3. **Build the project**: Run `scripts/build.sh`
4. **Start with Phase 1**: Follow the steps above
5. **Document everything**: Fill in `docs/research_notes.md` as you go
6. **Measure continuously**: Keep results organized in `results/` directory

Good luck! 🚀
