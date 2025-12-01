#include "RequestProcessor.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <algorithm>
#include <cstdio>

#if defined(__APPLE__) // macOS flag
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {
constexpr int kMaxGrpcMessageSize = 1536 * 1024 * 1024; // 1.5GB

uint64_t GetProcessMemory() {
#if defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<uint64_t>(info.resident_size);
#elif defined(__linux__)
    long rss = 0L;
    FILE* fp = fopen("/proc/self/statm", "r");
    if (fp && fscanf(fp, "%*s%ld", &rss) == 1) {
        fclose(fp);
        return static_cast<uint64_t>(rss) * sysconf(_SC_PAGESIZE);
    }
    if (fp) {
        fclose(fp);
    }
    return 0;
#else
    return 0;
#endif
}
}

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

RequestProcessor::RequestProcessor(const std::string& node_id) 
    : node_id_(node_id)
    , shutting_down_(false)
    , requests_processed_(0)
    , start_time_(std::chrono::steady_clock::now()) {
    std::cout << "[RequestProcessor] Node " << node_id << " ready" << std::endl;
}

RequestProcessor::~RequestProcessor() {
    // Nothing to clean up beyond automatic members
}

void RequestProcessor::SetTeamLeaders(const std::vector<std::pair<std::string, std::string>>& team_leader_endpoints) {
    for (const auto& [role, addr] : team_leader_endpoints) {
        RegisterPeer(addr, team_leader_stubs_, "team leader");
        team_leader_roles_[addr] = role;
    }
}

void RequestProcessor::SetWorkers(const std::vector<std::string>& worker_addresses) {
    for (const auto& addr : worker_addresses) {
        RegisterPeer(addr, worker_stubs_, "worker");
    }
}

void RequestProcessor::SetLeaderAddress(const std::string& leader_address) {
    auto channel = grpc::CreateCustomChannel(leader_address, grpc::InsecureChannelCredentials(), MakeLargeMessageArgs());
    leader_stub_ = mini2::TeamIngress::NewStub(channel);
    std::cout << "[RequestProcessor] Connected to leader: " << leader_address << std::endl;
}

void RequestProcessor::LoadDataset(const std::string& dataset_path) {
    std::lock_guard<std::mutex> lock(dataset_mutex_);

    if (dataset_path.empty()) {
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

bool RequestProcessor::HasDataset() const {
    std::lock_guard<std::mutex> lock(dataset_mutex_);
    return data_processor_ != nullptr;
}

std::shared_ptr<DataProcessor> RequestProcessor::GetDataProcessor() const {
    std::lock_guard<std::mutex> lock(dataset_mutex_);
    return data_processor_;
}

void RequestProcessor::LoadDatasetIfNeeded(const mini2::Request& request) {
    if (request.query().empty()) {
        return;
    }

    std::cout << "[" << node_id_ << "] Loading dataset from query: " << request.query() << std::endl;
    LoadDataset(request.query());
}

// ============================================================================
// Process A: Leader Request Handling
// ============================================================================

std::vector<mini2::WorkerResult> RequestProcessor::ProcessRequest(const mini2::Request& request) {
    std::cout << "[Leader] request: " << request.request_id() 
              << " green=" << request.need_green() 
              << " pink=" << request.need_pink() << std::endl;

    // Forward to team leaders
    int expected_results = ForwardToTeamLeaders(request, request.need_green(), request.need_pink());
    
    std::cout << "[Leader] waiting for " << expected_results << " team-leader result(s)" << std::endl;

    // Wait for results with condition variable (efficient waiting)
    std::unique_lock<std::mutex> lock(results_mutex_);
    bool got_results = results_cv_.wait_for(lock, std::chrono::seconds(90), [this, &request, expected_results]() {
        return pending_results_.count(request.request_id()) && 
               pending_results_[request.request_id()].size() >= static_cast<size_t>(expected_results);
    });
    
    if (!got_results) {
        std::cerr << "[Leader] WARNING: Timeout waiting for results from team leaders" << std::endl;
    } else {
        std::cout << "[Leader] received all expected results" << std::endl;
    }

    // Collect results (lock already held from wait_for)
    std::vector<mini2::WorkerResult> results;
    
    if (pending_results_.count(request.request_id())) {
        results = pending_results_[request.request_id()];
        pending_results_.erase(request.request_id());
    } else {
        // Fallback if results not received in time
        std::cerr << "[Leader] WARNING: No results received for " << request.request_id() 
                  << ", returning empty" << std::endl;
    }

    std::cout << "[Leader] done: " << request.request_id() 
              << " chunks=" << results.size() << std::endl;

    return results;
}

int RequestProcessor::ForwardToTeamLeaders(const mini2::Request& req, bool need_green, bool need_pink) {
    int forwarded = 0;
    for (auto& [addr, stub] : team_leader_stubs_) {
        ClientContext ctx;
        mini2::HeartbeatAck ack;
        
        const auto role_it = team_leader_roles_.find(addr);
        const std::string role = (role_it != team_leader_roles_.end()) ? role_it->second : "";
        const bool should_call =
            (role == "green" && need_green) ||
            (role == "pink" && need_pink) ||
            role.empty();

        if (should_call) {
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

void RequestProcessor::HandleTeamRequest(const mini2::Request& request) {
    std::cout << "[TeamLeader " << node_id_ << "] request: " << request.request_id() << std::endl;
    
    LoadDatasetIfNeeded(request);
    auto proc = GetDataProcessor();

    constexpr uint32_t kLocalPartitions = 2;
    const bool can_delegate = (proc != nullptr) && !worker_stubs_.empty();

    if (can_delegate) {
        std::cout << "[TeamLeader " << node_id_ << "] forwarding to " 
              << worker_stubs_.size() << " worker(s)" << std::endl;
        int expected_workers = ForwardToWorkers(request);
        
        std::cout << "[TeamLeader " << node_id_ << "] waiting for " << expected_workers 
                  << " worker result(s)" << std::endl;

        std::unique_lock<std::mutex> lock(results_mutex_);
        bool got_results = results_cv_.wait_for(lock, std::chrono::seconds(60), [this, &request, expected_workers]() {
            return pending_results_.count(request.request_id()) && 
                   pending_results_[request.request_id()].size() >= static_cast<size_t>(expected_workers);
        });
        
        if (!got_results) {
            std::cerr << "[TeamLeader " << node_id_ << "] WARNING: Timeout waiting for worker results, processing locally"
                      << std::endl;
            lock.unlock();
            ProcessLocally(proc, request, kLocalPartitions);
        } else {
            std::cout << "[TeamLeader " << node_id_ << "] got all " << expected_workers 
                      << " worker result(s)" << std::endl;
            lock.unlock();
        }
    } else {
        std::cout << "[TeamLeader " << node_id_ << "] processing locally (dataset=" 
              << (proc ? "yes" : "no") << ", workers=" << worker_stubs_.size() << ")" << std::endl;
        ProcessLocally(proc, request, kLocalPartitions);
    }

    std::cout << "[TeamLeader " << node_id_ << "] done: " << request.request_id() << std::endl;
    
    // Send results back to Process A (Leader)
    if (leader_stub_) {
        std::cout << "[TeamLeader " << node_id_ << "] sending results to leader" << std::endl;
        std::lock_guard<std::mutex> lock(results_mutex_);
        auto& results = pending_results_[request.request_id()];
        for (const auto& result : results) {
            ClientContext ctx;
            mini2::HeartbeatAck ack;
            Status status = leader_stub_->PushWorkerResult(&ctx, result, &ack);
            if (status.ok()) {
                std::cout << "[TeamLeader " << node_id_ << "] sent part " 
                             << result.part_index() << " to leader" << std::endl;
            } else {
                std::cerr << "[TeamLeader " << node_id_ << "] Failed to send result: " 
                         << status.error_message() << std::endl;
            }
        }
        pending_results_.erase(request.request_id());
    } else {
        std::cout << "[TeamLeader " << node_id_ << "] WARNING: leader stub not configured" << std::endl;
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

void RequestProcessor::HandleWorkerRequest(const mini2::Request& request) {
    std::cout << "[Worker " << node_id_ << "] request: " << request.request_id() << std::endl;

    // Generate result and send back to team leader
    auto result = GenerateWorkerResult(request);
    
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

mini2::WorkerResult RequestProcessor::GenerateWorkerResult(const mini2::Request& request) {
    std::cout << "[Worker " << node_id_ << "] generating result for: " << request.request_id() << std::endl;

    LoadDatasetIfNeeded(request);
    auto proc = GetDataProcessor();
    
    if (proc) {
        // Process real data
        size_t total_rows = proc->GetTotalRows();
        const int worker_num = (node_id_ == "C" ? 0 : (node_id_ == "D" ? 1 : 2)); // C=0, D=1, F=2
        const size_t worker_count = 3;

        if (total_rows == 0) {
            mini2::WorkerResult empty;
            empty.set_request_id(request.request_id());
            empty.set_part_index(worker_num);
            return empty;
        }

        size_t rows_per_worker = std::max<size_t>(1, total_rows / worker_count);
        size_t start_idx = static_cast<size_t>(worker_num) * rows_per_worker;
        if (start_idx >= total_rows) {
            start_idx = total_rows - 1;
        }

        size_t remaining = total_rows - start_idx;
        size_t count = (worker_num == worker_count - 1)
                           ? remaining
                           : std::min(rows_per_worker, remaining);

        return ProcessRealData(proc, request, start_idx, count);
    } else {
        // No dataset loaded
        mini2::WorkerResult empty;
        empty.set_request_id(request.request_id());
        empty.set_part_index(0);
        return empty;
    }
}

mini2::WorkerResult RequestProcessor::ProcessRealData(std::shared_ptr<DataProcessor> processor, const mini2::Request& req, size_t start_idx, size_t count) {
    std::cout << "[" << node_id_ << "] real-data chunk start=" << start_idx 
              << " count=" << count << std::endl;
    
    mini2::WorkerResult result;
    result.set_request_id(req.request_id());
    result.set_part_index(start_idx / count); // Simple part index calculation
    
    // Get data chunk
    auto chunk = processor->GetChunk(start_idx, count);
    
    // Process chunk (filter by parameter if specified in request)
    std::string filter_param = ""; // Could extract from request if needed
    std::string processed = processor->ProcessChunk(chunk, filter_param);
    
    // Set payload
    result.set_payload(processed);
    
    std::cout << "[" << node_id_ << "] generated " << processed.size() 
              << " bytes for part " << result.part_index() << std::endl;
    
    return result;
}



grpc::ChannelArguments RequestProcessor::MakeLargeMessageArgs() {
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(kMaxGrpcMessageSize);
    args.SetMaxSendMessageSize(kMaxGrpcMessageSize);
    return args;
}

void RequestProcessor::RegisterPeer(const std::string& addr,
                                    std::map<std::string, std::unique_ptr<mini2::TeamIngress::Stub>>& target,
                                    const char* label) {
    auto channel = grpc::CreateCustomChannel(addr, grpc::InsecureChannelCredentials(), MakeLargeMessageArgs());
    target[addr] = mini2::TeamIngress::NewStub(channel);
    if (label) {
        std::cout << "[RequestProcessor] Connected to " << label << ": " << addr << std::endl;
    }
}



void RequestProcessor::ProcessLocally(std::shared_ptr<DataProcessor> processor, const mini2::Request& request, uint32_t partitions) {
    const uint32_t parts = std::max<uint32_t>(1, partitions);

    if (processor) {
        if (processor->GetTotalRows() == 0) {
            // No rows to process
            return;
        }

        size_t total_rows = processor->GetTotalRows();
        size_t rows_per_part = std::max<size_t>(1, total_rows / parts);

        for (uint32_t i = 0; i < parts; ++i) {
            size_t start_idx = static_cast<size_t>(i) * rows_per_part;
            if (start_idx >= total_rows) {
                break;
            }

            size_t remaining = total_rows - start_idx;
            size_t count = (i == parts - 1) ? remaining : std::min(rows_per_part, remaining);
            
            // Process chunk
            mini2::WorkerResult result = ProcessRealData(processor, request, start_idx, count);
            
            // Store result locally
            ReceiveWorkerResult(result);
        }
    }
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



// ============================================================================
// Status and Control
// ============================================================================

mini2::StatusResponse RequestProcessor::GetStatus() const {
    mini2::StatusResponse status;
    status.set_node_id(node_id_);
    status.set_state(GetNodeState());

    uint32_t queue_size = 0;
    {
        std::lock_guard<std::mutex> lock(results_mutex_);
        for (const auto& pair : pending_results_) {
            queue_size += pair.second.size();
        }
    }
    status.set_queue_size(queue_size);
    
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

    size_t pending = 0;
    {
        std::lock_guard<std::mutex> lock(results_mutex_);
        for (const auto& pair : pending_results_) {
            pending += pair.second.size();
        }
    }

    if (pending == 0) {
        return "IDLE";
    }
    if (pending < 5) {
        return "BUSY";
    }
    return "OVERLOADED";
}

void RequestProcessor::InitiateShutdown(int delay_seconds) {
    std::cout << "[RequestProcessor:" << node_id_ << "] Initiating shutdown in " 
              << delay_seconds << " seconds..." << std::endl;
    
    shutting_down_ = true;
    
    // Allow pending work to complete
    if (delay_seconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
    }
    
    std::cout << "[RequestProcessor:" << node_id_ << "] Shutdown complete. "
              << "Total requests processed: " << requests_processed_ << std::endl;
}
