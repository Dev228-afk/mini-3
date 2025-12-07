#include "RequestProcessor.h"
#include "../common/logging.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

#if defined(__APPLE__) // macOS flag
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {
constexpr int kMaxGrpcMessageSize = 1536 * 1024 * 1024; // 1.5GB
constexpr std::chrono::milliseconds kTeamLeaderWaitTimeoutMs{10000}; // 10 seconds for team leaders
constexpr std::chrono::milliseconds kLeaderWaitTimeoutMs{12000};     // 12 seconds for global leader

// Helper to get slowdown for worker D (simulates weak hardware)
int getSlowdownMsForNode(const std::string& node_id) {
    const char* env = std::getenv("MINI3_SLOW_D_MS");
    if (!env) return 0;
    int ms = std::atoi(env);
    if (ms <= 0) return 0;
    // Only apply slowdown to node D
    if (node_id == "D") {
        return ms;
    }
    return 0;
}

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

void RequestProcessor::SetWorkers(const std::map<std::string, std::pair<std::string, int>>& worker_info) {
    for (const auto& [worker_id, info] : worker_info) {
        const auto& [addr, capacity_score] = info;
        RegisterPeer(addr, worker_stubs_, "worker");
        
        // Initialize worker stats
        std::lock_guard<std::mutex> lock(task_mutex_);
        WorkerStats& ws = worker_stats_[worker_id];
        ws.addr = addr;
        ws.capacity_score = capacity_score;
        ws.avg_task_ms = 0.0;
        ws.queue_len = 0;
        ws.last_heartbeat = std::chrono::steady_clock::now();
        ws.healthy = true;
        
        LOG_INFO(node_id_, "RequestProcessor", 
                 "Registered worker " + worker_id + " with capacity_score=" + 
                 std::to_string(capacity_score) + " at " + addr);
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

    // Wait for results with shorter timeout (12 seconds instead of 90)
    std::unique_lock<std::mutex> lock(results_mutex_);
    bool got_results = results_cv_.wait_for(lock, kLeaderWaitTimeoutMs, [this, &request, expected_results]() {
        return pending_results_.count(request.request_id()) && 
               pending_results_[request.request_id()].size() >= static_cast<size_t>(expected_results);
    });
    
    // Count successful teams based on results received and internal status
    int total_teams = expected_results;
    int successful_teams = 0;
    std::vector<std::string> failed_reasons;
    
    // Check which teams succeeded
    if (pending_results_.count(request.request_id())) {
        // Count teams that sent results
        size_t results_received = pending_results_[request.request_id()].size();
        if (results_received > 0) {
            successful_teams = 1; // At least one team sent results
            // Could be more sophisticated: track per-team results
        }
    }
    
    // Check for teams that timed out
    if (!got_results) {
        failed_reasons.push_back("Leader timeout (" + std::to_string(kLeaderWaitTimeoutMs.count()) + "ms)");
    }
    
    // Collect results (lock already held from wait_for)
    std::vector<mini2::WorkerResult> results;
    
    if (pending_results_.count(request.request_id())) {
        results = pending_results_[request.request_id()];
        pending_results_.erase(request.request_id());
    }
    
    // Log outcome based on success/failure
    if (successful_teams > 0 && successful_teams < total_teams) {
        // Partial success
        LOG_WARN(node_id_, "Leader", 
                 "Partial team failure for request " + request.request_id() + 
                 ": " + std::to_string(successful_teams) + "/" + 
                 std::to_string(total_teams) + " teams succeeded. Failures: " +
                 (failed_reasons.empty() ? "unknown" : failed_reasons[0]));
        std::cout << "[Leader] done (partial): " << request.request_id() 
                  << " chunks=" << results.size() << std::endl;
    } else if (successful_teams == 0) {
        // Total failure
        LOG_ERROR(node_id_, "Leader", 
                  "All teams failed for request " + request.request_id() + 
                  "; returning empty result. Reason: " + 
                  (failed_reasons.empty() ? "no results received" : failed_reasons[0]));
        std::cerr << "[Leader] ERROR: All teams failed for " << request.request_id() 
                  << ", returning empty result" << std::endl;
        std::cout << "[Leader] done: " << request.request_id() 
                  << " chunks=0" << std::endl;
    } else {
        // Full success
        std::cout << "[Leader] done: " << request.request_id() 
                  << " chunks=" << results.size() << std::endl;
    }

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
    LOG_INFO(node_id_, "RequestProcessor",
             "HandleTeamRequest: processing request_id=" + request.request_id() +
             " dataset=" + request.query());
    
    LoadDatasetIfNeeded(request);
    auto proc = GetDataProcessor();

    if (proc && !worker_stats_.empty()) {
        // Check if we have any healthy workers before creating tasks
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            size_t healthy_count = 0;
            for (const auto& [worker_id, ws] : worker_stats_) {
                if (ws.healthy) {
                    healthy_count++;
                }
            }
            
            if (healthy_count == 0) {
                LOG_WARN(node_id_, "TeamLeader", 
                         "No healthy workers available; failing request " + request.request_id() + " fast");
                // Mark this team request as failed internally
                {
                    std::lock_guard<std::mutex> status_lock(results_mutex_);
                    team_request_status_[request.request_id()].success = false;
                    team_request_status_[request.request_id()].failure_reason = "No healthy workers";
                }
                // Return immediately without creating tasks or waiting
                // The leader will see zero results and handle it accordingly
                return;
            }
            
            LOG_INFO(node_id_, "TeamLeader", 
                     "Request " + request.request_id() + " has " + std::to_string(healthy_count) + 
                     " healthy worker(s) available");
        }
        
        // Create tasks for workers to pull
        size_t total_rows = proc->GetTotalRows();
        size_t num_workers = worker_stats_.size();
        size_t num_tasks = num_workers * 3; // 3 tasks per worker
        
        if (total_rows == 0) {
            LOG_WARN(node_id_, "TeamLeader", "Dataset has 0 rows, cannot create tasks");
            // Fall back to local processing
            constexpr uint32_t kLocalPartitions = 2;
            ProcessLocally(proc, request, kLocalPartitions);
        } else {
            size_t rows_per_task = (total_rows + num_tasks - 1) / num_tasks;
            
            // Clear old tasks and create new ones with capacity-aware assignment
            {
                std::lock_guard<std::mutex> lock(task_mutex_);
                team_task_queue_.clear();
                
                // Clear worker queues for new request
                for (auto& [worker_id, queue] : worker_queues_) {
                    queue.clear();
                    worker_stats_[worker_id].queue_len = 0;
                }
                
                for (size_t i = 0; i < num_tasks; ++i) {
                    size_t start_row = i * rows_per_task;
                    if (start_row >= total_rows) break;
                    
                    size_t num_rows = std::min(rows_per_task, total_rows - start_row);
                    
                    mini2::Task task;
                    task.set_request_id(request.request_id());
                    task.set_session_id(request.request_id()); // Use request_id as session_id
                    task.set_chunk_id(i);
                    task.set_start_row(start_row);
                    task.set_num_rows(num_rows);
                    task.set_dataset_path(request.query());
                    
                    // Use capacity-aware assignment instead of team queue
                    std::string best_id = ChooseBestWorkerId();
                    if (!best_id.empty()) {
                        worker_queues_[best_id].push_back(task);
                        auto& ws = worker_stats_[best_id];
                        ws.queue_len = worker_queues_[best_id].size();
                        
                        LOG_DEBUG(node_id_, "RequestProcessor", 
                                  "Assigned task " + task.request_id() + "." + std::to_string(task.chunk_id()) + 
                                  " to worker " + best_id + 
                                  " (avg_ms=" + std::to_string(ws.avg_task_ms) + 
                                  ", queue=" + std::to_string(ws.queue_len) + ")");
                    } else {
                        // No healthy worker, fallback to team queue
                        team_task_queue_.push_back(task);
                    }
                }
                
                size_t assigned_count = 0;
                for (const auto& [worker_id, queue] : worker_queues_) {
                    assigned_count += queue.size();
                }
            }
            
            LOG_INFO(node_id_, "RequestProcessor",
                     "HandleTeamRequest: created and assigned tasks for request_id=" + request.request_id() + 
                     " (total_rows=" + std::to_string(total_rows) + ")");
            
            // Wait for workers to pull tasks and send results (10 second timeout)
            size_t expected_results = num_tasks;
            std::unique_lock<std::mutex> lock(results_mutex_);
            bool got_results = results_cv_.wait_for(lock, kTeamLeaderWaitTimeoutMs, 
                [this, &request, expected_results]() {
                    return pending_results_.count(request.request_id()) && 
                           pending_results_[request.request_id()].size() >= expected_results;
                });
            
            if (!got_results) {
                LOG_WARN(node_id_, "TeamLeader", 
                         "Timeout waiting for worker results for request " + request.request_id() +
                         " (waited " + std::to_string(kTeamLeaderWaitTimeoutMs.count()) + "ms)");
                // Mark this team request as failed internally
                team_request_status_[request.request_id()].success = false;
                team_request_status_[request.request_id()].failure_reason = "Timeout waiting for worker results";
            } else {
                LOG_INFO(node_id_, "TeamLeader", 
                         "Received all " + std::to_string(expected_results) + " results for request " + request.request_id());
                team_request_status_[request.request_id()].success = true;
            }
            lock.unlock();
        }
    } else {
        // No dataset or no workers - process locally
        LOG_INFO(node_id_, "TeamLeader", 
                 "Processing locally (dataset=" + std::string(proc ? "yes" : "no") + 
                 ", workers=" + std::to_string(worker_stats_.size()) + ")");
        constexpr uint32_t kLocalPartitions = 2;
        ProcessLocally(proc, request, kLocalPartitions);
    }

    LOG_INFO(node_id_, "TeamLeader", "Done processing request: " + request.request_id());
    
    // Send results back to Process A (Leader)
    if (leader_stub_) {
        LOG_INFO(node_id_, "TeamLeader", "Sending results to leader");
        std::lock_guard<std::mutex> lock(results_mutex_);
        auto& results = pending_results_[request.request_id()];
        for (const auto& result : results) {
            ClientContext ctx;
            mini2::HeartbeatAck ack;
            Status status = leader_stub_->PushWorkerResult(&ctx, result, &ack);
            if (status.ok()) {
                LOG_DEBUG(node_id_, "TeamLeader", 
                          "Sent part " + std::to_string(result.part_index()) + " to leader");
            } else {
                LOG_ERROR(node_id_, "TeamLeader", 
                          "Failed to send result: " + status.error_message());
            }
        }
        pending_results_.erase(request.request_id());
    } else {
        LOG_WARN(node_id_, "TeamLeader", "No leader stub available to send results");
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

mini2::WorkerResult RequestProcessor::ProcessTask(const mini2::Task& task, double& processing_time_ms) {
    auto start_time = std::chrono::steady_clock::now();
    
    LOG_DEBUG(node_id_, "Worker", 
              "Processing task " + task.request_id() + "." + std::to_string(task.chunk_id()));
    
    // Simulate slow hardware for worker D
    int slow_ms = getSlowdownMsForNode(node_id_);
    if (slow_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(slow_ms));
    }
    
    // Load dataset if needed
    LoadDataset(task.dataset_path());
    auto proc = GetDataProcessor();
    
    mini2::WorkerResult result;
    result.set_request_id(task.request_id());
    result.set_part_index(task.chunk_id());
    
    if (proc) {
        // Get data chunk
        auto chunk = proc->GetChunk(task.start_row(), task.num_rows());
        
        // Process chunk
        std::string processed = proc->ProcessChunk(chunk, "");
        result.set_payload(processed);
        
        LOG_DEBUG(node_id_, "Worker", 
                  "Generated " + std::to_string(processed.size()) + " bytes for task " + 
                  task.request_id() + "." + std::to_string(task.chunk_id()));
    } else {
        LOG_WARN(node_id_, "Worker", "No dataset loaded for task processing");
    }
    
    auto end_time = std::chrono::steady_clock::now();
    processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
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
    
    // Check initial connectivity
    auto state = channel->GetState(true);  // true = try to connect
    std::string state_str = (state == GRPC_CHANNEL_READY) ? "READY" : 
                           (state == GRPC_CHANNEL_CONNECTING) ? "CONNECTING" : "IDLE";
    
    target[addr] = mini2::TeamIngress::NewStub(channel);
    if (label) {
        std::cout << "[RequestProcessor] Registered " << label << ": " << addr 
                  << " (state=" << state_str << ")" << std::endl;
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

double RequestProcessor::ComputeWorkerRank(const WorkerStats& ws) const {
    const double alpha = 1.0;   // capacity weight
    const double beta  = 0.5;   // queue length penalty
    const double gamma = 0.001; // ms penalty
    return alpha * static_cast<double>(ws.capacity_score)
         - beta  * static_cast<double>(ws.queue_len)
         - gamma * ws.avg_task_ms;
}

void RequestProcessor::UpdateWorkerHeartbeat(const std::string& worker_id, double recent_task_ms, uint32_t queue_len) {
    std::lock_guard<std::mutex> lock(task_mutex_);
    
    auto it = worker_stats_.find(worker_id);
    if (it == worker_stats_.end()) {
        return;
    }
    
    WorkerStats& ws = it->second;
    ws.last_heartbeat = std::chrono::steady_clock::now();
    ws.queue_len = queue_len;
    
    // Mark worker as healthy when we receive a heartbeat
    if (!ws.healthy) {
        LOG_INFO(node_id_, "Heartbeat", "Worker " + worker_id + " is now HEALTHY (heartbeat received)");
        ws.healthy = true;
    }
    
    // Update avg_task_ms using exponential moving average
    if (recent_task_ms > 0.0) {
        ws.avg_task_ms = 0.8 * ws.avg_task_ms + 0.2 * recent_task_ms;
    }
    
    LOG_DEBUG(node_id_, "Heartbeat", 
              "Updated stats for " + worker_id + " (healthy=" + std::to_string(ws.healthy) + 
              "): avg_ms=" + std::to_string(ws.avg_task_ms) + 
              ", queue=" + std::to_string(ws.queue_len));
}

void RequestProcessor::MaintenanceTick() {
    constexpr int DEAD_TIMEOUT_SECONDS = 10;
    constexpr size_t MAX_QUEUE_PER_WORKER = 20;
    constexpr size_t MAX_TEAM_QUEUE = 100;
    
    std::lock_guard<std::mutex> lock(task_mutex_);
    auto now = std::chrono::steady_clock::now();
    
    // Check for dead workers
    for (auto& [worker_id, ws] : worker_stats_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ws.last_heartbeat).count();
        
        if (elapsed > DEAD_TIMEOUT_SECONDS && ws.healthy) {
            LOG_WARN(node_id_, "Maintenance", 
                     "Worker " + worker_id + " marked as DEAD (no heartbeat for " + 
                     std::to_string(elapsed) + "s)");
            
            // Reassign tasks from unhealthy worker
            OnWorkerBecameUnhealthy(worker_id);
        } else if (elapsed <= DEAD_TIMEOUT_SECONDS && !ws.healthy) {
            // Worker came back to life
            LOG_INFO(node_id_, "Maintenance", "Worker " + worker_id + " marked as HEALTHY");
            ws.healthy = true;
        }
    }
    
    // Check for overloaded workers
    for (const auto& [worker_id, worker_queue] : worker_queues_) {
        if (worker_queue.size() > MAX_QUEUE_PER_WORKER) {
            LOG_WARN(node_id_, "Maintenance", 
                     "Worker " + worker_id + " queue overloaded: " + 
                     std::to_string(worker_queue.size()) + " tasks (max=" + 
                     std::to_string(MAX_QUEUE_PER_WORKER) + ")");
        }
    }
    
    // Check team queue
    if (team_task_queue_.size() > MAX_TEAM_QUEUE) {
        LOG_WARN(node_id_, "Maintenance", 
                 "Team queue overloaded: " + std::to_string(team_task_queue_.size()) + 
                 " tasks (max=" + std::to_string(MAX_TEAM_QUEUE) + ")");
    }
}

mini2::Task RequestProcessor::RequestTaskForWorker(const std::string& worker_id) {
    std::lock_guard<std::mutex> lock(task_mutex_);
    
    // Debug: log known workers
    {
        std::ostringstream oss;
        oss << "RequestTaskForWorker called for worker_id=" << worker_id
            << " | known_workers=[";
        bool first = true;
        for (const auto& kv : worker_stats_) {
            if (!first) oss << ",";
            first = false;
            oss << kv.first << "(healthy=" << (kv.second.healthy ? "1" : "0")
                << ",queue=" << kv.second.queue_len << ")";
        }
        oss << "]";
        LOG_DEBUG(node_id_, "RequestProcessor", oss.str());
    }
    
    // Check if worker exists and is healthy
    auto ws_it = worker_stats_.find(worker_id);
    if (ws_it == worker_stats_.end() || !ws_it->second.healthy) {
        LOG_DEBUG(node_id_, "RequestProcessor", 
                  "RequestTask from unknown/unhealthy worker: " + worker_id);
        return mini2::Task(); // empty task
    }
    
    // 1. Check worker's own queue first
    auto& worker_queue = worker_queues_[worker_id];
    if (!worker_queue.empty()) {
        mini2::Task task = worker_queue.front();
        worker_queue.pop_front();
        ws_it->second.queue_len = worker_queue.size();
        LOG_DEBUG(node_id_, "RequestProcessor",
                  "Assigning task " + task.request_id() + "." + std::to_string(task.chunk_id()) +
                  " to worker " + worker_id + " from OWN_QUEUE");
        return task;
    }
    
    // 2. Try to steal from other workers
    mini2::Task stolen_task;
    if (TryStealTask(worker_id, stolen_task)) {
        LOG_DEBUG(node_id_, "RequestProcessor",
                  "Assigning task " + stolen_task.request_id() + "." + std::to_string(stolen_task.chunk_id()) +
                  " to worker " + worker_id + " via STEAL");
        return stolen_task;
    }
    
    // 3. Check team task queue
    if (!team_task_queue_.empty()) {
        mini2::Task task = team_task_queue_.front();
        team_task_queue_.pop_front();
        LOG_DEBUG(node_id_, "RequestProcessor",
                  "Assigning task " + task.request_id() + "." + std::to_string(task.chunk_id()) +
                  " to worker " + worker_id + " from TEAM_QUEUE");
        return task;
    }
    
    // No work available
    LOG_DEBUG(node_id_, "RequestProcessor", "No tasks available for " + worker_id);
    return mini2::Task(); // empty task
}

void RequestProcessor::EnsureWorkerRegistered(const std::string& worker_id) {
    // Only relevant for team leaders
    if (node_id_ != "B" && node_id_ != "E") return;

    std::lock_guard<std::mutex> lock(task_mutex_);
    auto it = worker_stats_.find(worker_id);
    if (it == worker_stats_.end()) {
        WorkerStats ws;
        ws.addr           = ""; // unknown, filled from config if available
        ws.capacity_score = 1;
        ws.avg_task_ms    = 0.0;
        ws.queue_len      = 0;
        ws.last_heartbeat = std::chrono::steady_clock::now();
        ws.healthy        = true;
        worker_stats_[worker_id] = ws;

        LOG_WARN(node_id_, "RequestProcessor",
                 "Auto-registered worker " + worker_id +
                 " on first contact (no config match)");
    } else {
        it->second.last_heartbeat = std::chrono::steady_clock::now();
        it->second.healthy        = true;
    }
}

bool RequestProcessor::TryStealTask(const std::string& thief_id, mini2::Task& out_task) {
    constexpr size_t HIGH_WATERMARK = 4;
    
    std::string best_donor;
    size_t max_queue_len = 0;
    
    // Find the donor with the largest queue length above HIGH_WATERMARK
    for (const auto& [donor_id, donor_queue] : worker_queues_) {
        if (donor_id == thief_id) continue;
        
        auto ws_it = worker_stats_.find(donor_id);
        if (ws_it == worker_stats_.end() || !ws_it->second.healthy) continue;
        
        if (donor_queue.size() > HIGH_WATERMARK && donor_queue.size() > max_queue_len) {
            best_donor = donor_id;
            max_queue_len = donor_queue.size();
        }
    }
    
    if (best_donor.empty()) {
        return false;
    }
    
    // Steal one task from the back of the donor's queue
    auto& donor_queue = worker_queues_[best_donor];
    out_task = donor_queue.back();
    donor_queue.pop_back();
    worker_stats_[best_donor].queue_len = donor_queue.size();
    
    LOG_DEBUG(node_id_, "RequestProcessor", 
              thief_id + " stole task " + out_task.request_id() + "." + 
              std::to_string(out_task.chunk_id()) + " from " + best_donor + 
              " (donor queue: " + std::to_string(donor_queue.size()) + ")");
    
    return true;
}

void RequestProcessor::OnWorkerBecameUnhealthy(const std::string& worker_id) {
    // Called with task_mutex_ already locked
    auto& ws = worker_stats_[worker_id];
    auto& worker_queue = worker_queues_[worker_id];
    size_t num_tasks = worker_queue.size();
    
    if (num_tasks == 0) {
        ws.healthy = false;
        ws.queue_len = 0;
        return;
    }
    
    LOG_WARN(node_id_, "TeamLeader", 
             "Worker " + worker_id + " became unhealthy; reassigning its " + 
             std::to_string(num_tasks) + " pending tasks.");
    
    // Reassign all pending tasks to healthy workers
    while (!worker_queue.empty()) {
        auto task = std::move(worker_queue.front());
        worker_queue.pop_front();
        
        // Try to assign to a healthy worker
        std::string best_id = ChooseBestWorkerId();
        if (!best_id.empty() && best_id != worker_id) {
            worker_queues_[best_id].push_back(task);
            worker_stats_[best_id].queue_len = worker_queues_[best_id].size();
            LOG_DEBUG(node_id_, "TeamLeader", 
                      "Reassigned task " + task.request_id() + "." + std::to_string(task.chunk_id()) + 
                      " from " + worker_id + " to " + best_id);
        } else {
            // No healthy worker available, put in team queue
            team_task_queue_.push_back(task);
        }
    }
    
    ws.healthy = false;
    ws.queue_len = 0;
}

std::string RequestProcessor::ChooseBestWorkerId() const {
    // Called with task_mutex_ already locked
    std::string best_id;
    double best_score = std::numeric_limits<double>::max();
    
    for (const auto& [id, info] : worker_stats_) {
        if (!info.healthy) continue;
        
        // Base score from avg latency; if no latency yet, use a default
        double latency = (info.avg_task_ms > 0.0) ? info.avg_task_ms : 100.0;
        
        // Add a penalty for queue length (factor 50ms per queued task)
        double score = latency + info.queue_len * 50.0;
        
        if (score < best_score) {
            best_score = score;
            best_id = id;
        }
    }
    
    return best_id;
}

