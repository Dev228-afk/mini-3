#include "RequestProcessor.h"
#include "../common/MemoryTracker.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

RequestProcessor::RequestProcessor(const std::string& nodeId) 
    : node_id_(nodeId)
    , shutting_down_(false)
    , requests_processed_(0)
    , health_check_running_(false)
    , health_check_interval_(10)
    , shm_update_running_(false)
    , start_time_(std::chrono::steady_clock::now()) {
    std::cout << "[RequestProcessor] Initialized for node " << nodeId << std::endl;
}

RequestProcessor::~RequestProcessor() {
    StopHealthCheckThread();
    
    // Stop shared memory update thread
    if (shm_update_running_) {
        shm_update_running_ = false;
        if (shm_update_thread_.joinable()) {
            shm_update_thread_.join();
        }
    }
    
    // Cleanup shared memory
    if (shm_coordinator_) {
        shm_coordinator_->Cleanup();
    }
    
    if (worker_queue_) {
        worker_queue_->Stop();
    }
}

void RequestProcessor::SetTeamLeaders(const std::vector<std::string>& team_leader_addresses) {
    for (const auto& addr : team_leader_addresses) {
        grpc::ChannelArguments args;
        args.SetMaxReceiveMessageSize(1536 * 1024 * 1024); // 1.5GB
        args.SetMaxSendMessageSize(1536 * 1024 * 1024);    // 1.5GB
        auto channel = grpc::CreateCustomChannel(addr, grpc::InsecureChannelCredentials(), args);
        team_leader_stubs_[addr] = mini2::TeamIngress::NewStub(channel);
        health_check_stubs_[addr] = mini2::NodeControl::NewStub(channel);
        std::cout << "[RequestProcessor] Connected to team leader: " << addr << std::endl;
    }
}

void RequestProcessor::SetWorkers(const std::vector<std::string>& worker_addresses) {
    for (const auto& addr : worker_addresses) {
        grpc::ChannelArguments args;
        args.SetMaxReceiveMessageSize(1536 * 1024 * 1024); // 1.5GB
        args.SetMaxSendMessageSize(1536 * 1024 * 1024);    // 1.5GB
        auto channel = grpc::CreateCustomChannel(addr, grpc::InsecureChannelCredentials(), args);
        worker_stubs_[addr] = mini2::TeamIngress::NewStub(channel);
        health_check_stubs_[addr] = mini2::NodeControl::NewStub(channel);
        std::cout << "[RequestProcessor] Connected to worker: " << addr << std::endl;
    }
}

void RequestProcessor::SetLeaderAddress(const std::string& leader_address) {
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(1536 * 1024 * 1024); // 1.5GB
    args.SetMaxSendMessageSize(1536 * 1024 * 1024);    // 1.5GB
    auto channel = grpc::CreateCustomChannel(leader_address, grpc::InsecureChannelCredentials(), args);
    leader_stub_ = mini2::TeamIngress::NewStub(channel);
    health_check_stubs_[leader_address] = mini2::NodeControl::NewStub(channel);
    std::cout << "[RequestProcessor] Connected to leader: " << leader_address << std::endl;
}

void RequestProcessor::LoadDataset(const std::string& dataset_path) {
    // Skip loading for mock data
    if (dataset_path.empty() || dataset_path == "mock_data") {
        std::cout << "[RequestProcessor] Skipping dataset load (using mock data)" << std::endl;
        return;
    }
    
    // Check if we need to reload (different dataset or not loaded yet)
    if (current_dataset_path_ == dataset_path && data_processor_ != nullptr) {
        std::cout << "[RequestProcessor] Dataset already loaded: " << dataset_path << std::endl;
        return;
    }
    
    std::cout << "[RequestProcessor] Loading dataset: " << dataset_path << std::endl;
    data_processor_ = std::make_unique<DataProcessor>(dataset_path);
    if (!data_processor_->LoadDataset()) {
        std::cerr << "[RequestProcessor] ERROR: Failed to load dataset" << std::endl;
        data_processor_ = nullptr;
        current_dataset_path_ = "";
    } else {
        current_dataset_path_ = dataset_path;
        std::cout << "[RequestProcessor] Dataset loaded successfully: " 
                  << data_processor_->GetTotalRows() << " rows" << std::endl;
    }
}

// ============================================================================
// Process A: Leader Request Handling
// ============================================================================

mini2::AggregatedResult RequestProcessor::ProcessRequestOnce(const mini2::Request& req) {
    std::cout << "[Leader] Processing RequestOnce: " << req.request_id() 
              << " (green=" << req.need_green() << ", pink=" << req.need_pink() << ")" << std::endl;

    // Forward to team leaders
    int expected_results = ForwardToTeamLeaders(req, req.need_green(), req.need_pink());
    
    std::cout << "[Leader] Waiting for " << expected_results << " team leader results..." << std::endl;

    // Wait for results with condition variable (efficient waiting)
    std::unique_lock<std::mutex> lock(results_mutex_);
    bool got_results = results_cv_.wait_for(lock, std::chrono::seconds(90), [this, &req, expected_results]() {
        return pending_results_.count(req.request_id()) && 
               pending_results_[req.request_id()].size() >= (size_t)expected_results;
    });
    
    if (!got_results) {
        std::cerr << "[Leader] WARNING: Timeout waiting for results from team leaders" << std::endl;
    } else {
        std::cout << "[Leader] Received all expected results" << std::endl;
    }

    // Collect and combine results (lock already held from wait_for)
    mini2::AggregatedResult aggregated;
    aggregated.set_request_id(req.request_id());
    
    if (pending_results_.count(req.request_id())) {
        aggregated = CombineResults(pending_results_[req.request_id()]);
        pending_results_.erase(req.request_id());
    } else {
        // Fallback if results not received in time
        std::cerr << "[Leader] WARNING: No results received for " << req.request_id() 
                  << ", returning mock data" << std::endl;
        aggregated.set_total_rows(1000);
        aggregated.set_total_bytes(50000);
    }

    std::cout << "[Leader] Completed RequestOnce: " << req.request_id() 
              << " (rows=" << aggregated.total_rows() << ", bytes=" << aggregated.total_bytes() << ")" << std::endl;

    return aggregated;
}

int RequestProcessor::ForwardToTeamLeaders(const mini2::Request& req, bool need_green, bool need_pink) {
    int forwarded = 0;
    for (auto& [addr, stub] : team_leader_stubs_) {
        ClientContext ctx;
        mini2::HeartbeatAck ack;
        
        // Determine if this team leader should process the request
        bool is_green = addr.find("50051") != std::string::npos; // Process B
        bool is_pink = addr.find("50054") != std::string::npos;  // Process E
        
        if ((is_green && need_green) || (is_pink && need_pink)) {
            Status status = stub->HandleRequest(&ctx, req, &ack);
            if (status.ok()) {
                std::cout << "[Leader] Forwarded to team leader: " << addr << std::endl;
                forwarded++;
            } else {
                std::cerr << "[Leader] Failed to forward to " << addr << ": " 
                         << status.error_message() << std::endl;
            }
        }
    }
    std::cout << "[Leader] Forwarded request to " << forwarded << " team leader(s)" << std::endl;
    return forwarded;
}

// ============================================================================
// Team Leaders: Request Forwarding
// ============================================================================

void RequestProcessor::HandleTeamRequest(const mini2::Request& req) {
    std::cout << "[TeamLeader " << node_id_ << "] Handling request: " << req.request_id() << std::endl;
    
    // Load dataset on-demand from Request.query field if provided
    if (!req.query().empty()) {
        std::cout << "[TeamLeader " << node_id_ << "] Loading dataset from query: " << req.query() << std::endl;
        LoadDataset(req.query());
    }

    if (HasDataset() && !worker_stubs_.empty()) {
        // Real data processing: Forward to workers
        std::cout << "[TeamLeader " << node_id_ << "] Forwarding to " 
                  << worker_stubs_.size() << " worker(s)..." << std::endl;
        int expected_workers = ForwardToWorkers(req);
        
        std::cout << "[TeamLeader " << node_id_ << "] Waiting for " << expected_workers 
                  << " worker results..." << std::endl;
        
        // Wait for worker results with condition variable (efficient waiting)
        std::unique_lock<std::mutex> lock(results_mutex_);
        bool got_results = results_cv_.wait_for(lock, std::chrono::seconds(300), 
            [this, &req, expected_workers]() {
                return pending_results_.count(req.request_id()) && 
                       pending_results_[req.request_id()].size() >= (size_t)expected_workers;
            });
        
        if (!got_results) {
            std::cerr << "[TeamLeader " << node_id_ << "] WARNING: Timeout waiting for worker results, processing locally" 
                      << std::endl;
            lock.unlock();
            
            // Process locally as fallback
            if (HasDataset()) {
                size_t total_rows = data_processor_->GetTotalRows();
                size_t rows_per_part = total_rows / 2;
                
                for (uint32_t i = 0; i < 2; i++) {
                    auto worker_result = ProcessRealData(req, i * rows_per_part, rows_per_part);
                    ReceiveWorkerResult(worker_result);
                }
            }
        } else {
            std::cout << "[TeamLeader " << node_id_ << "] Received all " << expected_workers 
                      << " worker results" << std::endl;
            lock.unlock();
        }
        
    } else {
        // Fallback: Team leader processes data directly
        std::cout << "[TeamLeader " << node_id_ << "] Processing locally (HasDataset=" 
                  << (HasDataset() ? "true" : "false") << ", workers=" << worker_stubs_.size() << ")" << std::endl;
        
        if (HasDataset()) {
            // Process real data locally
            size_t total_rows = data_processor_->GetTotalRows();
            size_t rows_per_part = total_rows / 2;
            
            for (uint32_t i = 0; i < 2; i++) {
                auto worker_result = ProcessRealData(req, i * rows_per_part, rows_per_part);
                ReceiveWorkerResult(worker_result);
            }
        } else {
            // Generate mock data (Phase 2 behavior)
            for (uint32_t i = 0; i < 2; i++) {
                auto worker_result = GenerateMockData(req.request_id(), i);
                ReceiveWorkerResult(worker_result);
            }
        }
    }

    std::cout << "[TeamLeader " << node_id_ << "] Completed processing for: " << req.request_id() << std::endl;
    
    // Send results back to Process A (Leader)
    if (leader_stub_) {
        std::cout << "[TeamLeader " << node_id_ << "] Sending results to leader" << std::endl;
        std::lock_guard<std::mutex> lock(results_mutex_);
        auto& results = pending_results_[req.request_id()];
        for (const auto& result : results) {
            ClientContext ctx;
            mini2::HeartbeatAck ack;
            Status status = leader_stub_->PushWorkerResult(&ctx, result, &ack);
            if (status.ok()) {
                std::cout << "[TeamLeader " << node_id_ << "] Sent result part " 
                         << result.part_index() << " to leader" << std::endl;
            } else {
                std::cerr << "[TeamLeader " << node_id_ << "] Failed to send result: " 
                         << status.error_message() << std::endl;
            }
        }
        // Clear the results after sending to avoid duplicates
        pending_results_.erase(req.request_id());
    } else {
        std::cout << "[TeamLeader " << node_id_ << "] WARNING: No leader stub configured" << std::endl;
    }
}

int RequestProcessor::ForwardToWorkers(const mini2::Request& req) {
    int forwarded = 0;
    for (auto& [addr, stub] : worker_stubs_) {
        ClientContext ctx;
        mini2::HeartbeatAck ack;
        
        Status status = stub->HandleRequest(&ctx, req, &ack);
        if (status.ok()) {
            std::cout << "[TeamLeader " << node_id_ << "] Forwarded to worker: " << addr << std::endl;
            forwarded++;
        } else {
            std::cerr << "[TeamLeader " << node_id_ << "] Failed to forward to " << addr << ": " 
                     << status.error_message() << std::endl;
        }
    }
    std::cout << "[TeamLeader " << node_id_ << "] Forwarded request to " << forwarded << " worker(s)" << std::endl;
    return forwarded;
}

// ============================================================================
// Workers: Result Generation
// ============================================================================

void RequestProcessor::HandleWorkerRequest(const mini2::Request& req) {
    std::cout << "[Worker " << node_id_ << "] Handling request: " << req.request_id() << std::endl;
    
    // Load dataset on-demand from Request.query field if provided
    if (!req.query().empty()) {
        std::cout << "[Worker " << node_id_ << "] Loading dataset from query: " << req.query() << std::endl;
        LoadDataset(req.query());
    }
    
    // Generate result and send back to team leader
    auto result = GenerateWorkerResult(req);
    
    // Send result back to team leader via PushWorkerResult
    if (leader_stub_) {
        ClientContext ctx;
        mini2::HeartbeatAck ack;
        Status status = leader_stub_->PushWorkerResult(&ctx, result, &ack);
        if (status.ok()) {
            std::cout << "[Worker " << node_id_ << "] Sent result to team leader" << std::endl;
        } else {
            std::cerr << "[Worker " << node_id_ << "] Failed to send result: " 
                     << status.error_message() << std::endl;
        }
    }
}

mini2::WorkerResult RequestProcessor::GenerateWorkerResult(const mini2::Request& req) {
    std::cout << "[Worker " << node_id_ << "] Generating result for: " << req.request_id() << std::endl;
    
    // Load dataset on-demand from Request.query field if provided
    if (!req.query().empty()) {
        std::cout << "[Worker " << node_id_ << "] Loading dataset from query: " << req.query() << std::endl;
        LoadDataset(req.query());
    }
    
    if (HasDataset()) {
        // Process real data
        size_t total_rows = data_processor_->GetTotalRows();
        // Each worker processes a portion of the data
        // We'll use node_id to determine which portion
        int worker_num = (node_id_ == "C" ? 0 : (node_id_ == "D" ? 1 : 2)); // C=0, D=1, F=2
        size_t rows_per_worker = total_rows / 3;
        size_t start_idx = worker_num * rows_per_worker;
        size_t count = (worker_num == 2) ? (total_rows - start_idx) : rows_per_worker;
        
        return ProcessRealData(req, start_idx, count);
    } else {
        // Fallback to mock data
        return GenerateMockData(req.request_id(), 0);
    }
}

mini2::WorkerResult RequestProcessor::ProcessRealData(const mini2::Request& req, size_t start_idx, size_t count) {
    std::cout << "[" << node_id_ << "] Processing real data: start=" << start_idx 
              << ", count=" << count << std::endl;
    
    mini2::WorkerResult result;
    result.set_request_id(req.request_id());
    result.set_part_index(start_idx / count); // Simple part index calculation
    
    // Get data chunk
    auto chunk = data_processor_->GetChunk(start_idx, count);
    
    // Process chunk (filter by parameter if specified in request)
    std::string filter_param = ""; // Could extract from request if needed
    std::string processed = data_processor_->ProcessChunk(chunk, filter_param);
    
    // Set payload
    result.set_payload(processed);
    
    std::cout << "[" << node_id_ << "] Generated " << processed.size() 
              << " bytes for part " << result.part_index() << std::endl;
    
    return result;
}

mini2::WorkerResult RequestProcessor::GenerateMockData(const std::string& request_id, uint32_t part_index) {
    mini2::WorkerResult result;
    result.set_request_id(request_id);
    result.set_part_index(part_index);
    
    // Generate realistic payload (simulating processed data)
    std::stringstream ss;
    ss << "Node:" << node_id_ << "|Part:" << part_index << "|";
    ss << "Data:";
    for (int i = 0; i < 100; i++) {
        ss << i * part_index << ",";
    }
    
    std::string payload_str = ss.str();
    result.set_payload(payload_str.data(), payload_str.size());
    
    std::cout << "[Worker " << node_id_ << "] Generated " << payload_str.size() 
              << " bytes for part " << part_index << std::endl;
    
    return result;
}

// ============================================================================
// Team Leaders: Result Collection
// ============================================================================

void RequestProcessor::ReceiveWorkerResult(const mini2::WorkerResult& result) {
    std::lock_guard<std::mutex> lock(results_mutex_);
    pending_results_[result.request_id()].push_back(result);
    
    std::cout << "[TeamLeader " << node_id_ << "] Received worker result for: " 
              << result.request_id() << " part=" << result.part_index() << std::endl;
    
    // Notify waiting threads that a result arrived
    results_cv_.notify_all();
}

mini2::AggregatedResult RequestProcessor::GetTeamAggregatedResult(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(results_mutex_);
    
    if (pending_results_.count(request_id)) {
        auto aggregated = CombineResults(pending_results_[request_id]);
        pending_results_.erase(request_id);
        return aggregated;
    }
    
    // Return empty result if not found
    mini2::AggregatedResult empty;
    empty.set_request_id(request_id);
    empty.set_total_rows(0);
    empty.set_total_bytes(0);
    return empty;
}

// ============================================================================
// Helper: Combine Results
// ============================================================================

mini2::AggregatedResult RequestProcessor::CombineResults(const std::vector<mini2::WorkerResult>& results) {
    mini2::AggregatedResult aggregated;
    
    if (!results.empty()) {
        aggregated.set_request_id(results[0].request_id());
    }
    
    uint64_t total_rows = 0;
    uint64_t total_bytes = 0;
    
    for (const auto& result : results) {
        total_bytes += result.payload().size();
        
        // Count actual rows in CSV payload (each line = 1 row, minus header)
        const std::string& payload = result.payload();
        int row_count = 0;
        bool first_line = true;
        for (size_t i = 0; i < payload.size(); i++) {
            if (payload[i] == '\n') {
                if (first_line) {
                    first_line = false; // Skip header
                } else {
                    row_count++;
                }
            }
        }
        total_rows += row_count;
        
        std::cout << "[RequestProcessor] Chunk part=" << result.part_index() 
                  << " has " << row_count << " rows, " << result.payload().size() << " bytes" << std::endl;
        
        // For Strategy A: add chunk
        aggregated.add_chunks(result.payload());
    }
    
    aggregated.set_total_rows(total_rows);
    aggregated.set_total_bytes(total_bytes);
    
    std::cout << "[Aggregation] Combined " << results.size() << " results: "
              << "rows=" << total_rows << ", bytes=" << total_bytes << std::endl;
    
    return aggregated;
}

// ============================================================================
// Worker Queue Management
// ============================================================================

void RequestProcessor::StartWorkerQueue(int num_threads) {
    if (worker_queue_) {
        std::cout << "[RequestProcessor] Worker queue already started" << std::endl;
        return;
    }
    
    worker_queue_ = std::make_unique<WorkerQueue>(node_id_, num_threads);
    
    // Share data processor with worker queue
    if (data_processor_) {
        worker_queue_->SetDataProcessor(data_processor_);
    }
    
    worker_queue_->Start();
    std::cout << "[RequestProcessor] Started worker queue with " << num_threads << " threads" << std::endl;
}

void RequestProcessor::StopWorkerQueue() {
    if (worker_queue_) {
        worker_queue_->Stop();
        worker_queue_.reset();
        std::cout << "[RequestProcessor] Stopped worker queue" << std::endl;
    }
}

// ============================================================================
// Status and Control
// ============================================================================

mini2::StatusResponse RequestProcessor::GetStatus() const {
    mini2::StatusResponse status;
    status.set_node_id(node_id_);
    status.set_state(GetNodeState());
    
    if (worker_queue_) {
        status.set_queue_size(worker_queue_->GetQueueSize());
    } else {
        status.set_queue_size(0);
    }
    
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    status.set_uptime_seconds(uptime);
    status.set_requests_processed(requests_processed_);
    
    // Get current memory usage (cross-platform)
    status.set_memory_bytes(GetProcessMemory());
    
    return status;
}

std::string RequestProcessor::GetNodeState() const {
    if (shutting_down_) {
        return "SHUTTING_DOWN";
    }
    
    if (worker_queue_) {
        size_t queue_size = worker_queue_->GetQueueSize();
        if (queue_size == 0 && worker_queue_->IsIdle()) {
            return "IDLE";
        } else if (queue_size < 5) {
            return "BUSY";
        } else {
            return "OVERLOADED";
        }
    }
    
    return "IDLE";
}

void RequestProcessor::InitiateShutdown(int delay_seconds) {
    std::cout << "[RequestProcessor:" << node_id_ << "] Initiating shutdown in " 
              << delay_seconds << " seconds..." << std::endl;
    
    shutting_down_ = true;
    
    // Allow pending work to complete
    if (delay_seconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
    }
    
    // Stop worker queue if exists
    StopWorkerQueue();
    
    std::cout << "[RequestProcessor:" << node_id_ << "] Shutdown complete. "
              << "Total requests processed: " << requests_processed_ << std::endl;
}

// ============================================================================
// Autonomous Health Check Implementation (Student-Level)
// ============================================================================

void RequestProcessor::StartHealthCheckThread(int interval_seconds) {
    health_check_interval_ = interval_seconds;
    health_check_running_ = true;
    health_check_thread_ = std::thread(&RequestProcessor::HealthCheckThreadFunc, this);
    std::cout << "[" << node_id_ << "] Health check thread started (interval: " 
              << interval_seconds << "s)" << std::endl;
}

void RequestProcessor::StopHealthCheckThread() {
    health_check_running_ = false;
    if (health_check_thread_.joinable()) {
        health_check_thread_.join();
        std::cout << "[" << node_id_ << "] Health check thread stopped" << std::endl;
    }
}

void RequestProcessor::HealthCheckThreadFunc() {
    std::cout << "[" << node_id_ << "] Health check thread running autonomously..." << std::endl;
    
    while (health_check_running_) {
        // Sleep first to allow initialization
        std::this_thread::sleep_for(std::chrono::seconds(health_check_interval_));
        
        if (!health_check_running_) break;
        
        // Perform autonomous health checks on connected neighbors
        std::cout << "[" << node_id_ << "] 🔍 Autonomous health check started" << std::endl;
        
        int total_checked = 0;
        int successful = 0;
        int failed = 0;
        
        // Check all neighbors using health_check_stubs_
        for (auto& [addr, stub] : health_check_stubs_) {
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
            
            mini2::Heartbeat hb;
            hb.set_from(node_id_);
            hb.set_ts_unix_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            
            mini2::HeartbeatAck ack;
            grpc::Status status = stub->Ping(&ctx, hb, &ack);
            
            total_checked++;
            if (status.ok()) {
                successful++;
                std::cout << "  ✓ " << addr << " - healthy" << std::endl;
            } else {
                failed++;
                std::cout << "  ✗ " << addr << " - unreachable (" 
                         << status.error_message() << ")" << std::endl;
            }
        }
        
        std::cout << "[" << node_id_ << "] Health check complete: " 
                  << successful << "/" << total_checked << " neighbors healthy";
        if (failed > 0) {
            std::cout << " (⚠️ " << failed << " failed)";
        }
        std::cout << std::endl << std::endl;
    }
    
    std::cout << "[" << node_id_ << "] Health check thread exiting..." << std::endl;
}

// ============================================================================
// Shared Memory Coordination (Phase 4)
// ============================================================================

void RequestProcessor::InitializeSharedMemory(const std::string& segment_name, 
                                              const std::vector<std::string>& member_ids) {
    std::cout << "[" << node_id_ << "] 🧠 Initializing shared memory: " << segment_name << std::endl;
    
    shm_coordinator_ = std::make_unique<SharedMemoryCoordinator>();
    
    if (!shm_coordinator_->Initialize(segment_name, member_ids)) {
        std::cerr << "[" << node_id_ << "] ⚠️  Failed to initialize shared memory" << std::endl;
        shm_coordinator_.reset();
        return;
    }
    
    std::cout << "[" << node_id_ << "] ✅ Shared memory initialized successfully" << std::endl;
    
    // Start background thread to update shared memory status
    shm_update_running_ = true;
    shm_update_thread_ = std::thread(&RequestProcessor::SharedMemoryUpdateThreadFunc, this);
}

void RequestProcessor::SharedMemoryUpdateThreadFunc() {
    std::cout << "[" << node_id_ << "] 🔄 Shared memory update thread started" << std::endl;
    
    while (shm_update_running_) {
        UpdateSharedMemoryStatus();
        std::this_thread::sleep_for(std::chrono::seconds(2)); // Update every 2 seconds
    }
    
    std::cout << "[" << node_id_ << "] 🔄 Shared memory update thread stopped" << std::endl;
}

void RequestProcessor::UpdateSharedMemoryStatus() {
    if (!shm_coordinator_ || !shm_coordinator_->IsInitialized()) {
        return;
    }
    
    // Determine current state
    ProcessStatus::State state = shutting_down_ ? ProcessStatus::SHUTDOWN : 
                                 (GetQueueSize() > 0 ? ProcessStatus::BUSY : ProcessStatus::IDLE);
    
    // Get current queue size
    uint32_t queue_size = GetQueueSize();
    
    // Get memory usage
    uint64_t memory_bytes = GetProcessMemory();
    
    // Update shared memory
    shm_coordinator_->UpdateStatus(state, queue_size, memory_bytes);
}

uint32_t RequestProcessor::GetQueueSize() const {
    // Return number of pending results as a proxy for queue size
    // Note: We can't lock here in const method, so make best effort read
    uint32_t total = 0;
    // Commenting out lock for const method - this is an approximate read
    // std::lock_guard<std::mutex> lock(results_mutex_);
    for (const auto& pair : pending_results_) {
        total += pair.second.size();
    }
    return total;
}

std::string RequestProcessor::FindLeastLoadedTeamLeader(const std::string& team) {
    if (!shm_coordinator_ || !shm_coordinator_->IsInitialized()) {
        // Fallback to round-robin or first available
        std::cout << "[" << node_id_ << "] ⚠️  Shared memory not available, using default routing" 
                  << std::endl;
        return "";
    }
    
    // Get all statuses from shared memory
    auto statuses = shm_coordinator_->GetAllStatuses();
    
    // Find least loaded process
    std::string best_process = shm_coordinator_->FindLeastLoadedProcess();
    
    if (!best_process.empty()) {
        std::cout << "[" << node_id_ << "] Load-aware routing selected: " << best_process 
                  << " for team " << team << std::endl;
                  
        // Print status info for debugging
        for (const auto& ps : statuses) {
            std::string proc_id(ps.process_id);
            std::cout << "[" << node_id_ << "]   " << proc_id 
                      << " - State: " << (ps.state == ProcessStatus::IDLE ? "IDLE" : "BUSY")
                      << ", Queue: " << ps.queue_size << std::endl;
        }
    }
    
    return best_process;
}
