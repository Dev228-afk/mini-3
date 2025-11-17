#pragma once

#include <grpcpp/grpcpp.h>
#include "minitwo.grpc.pb.h"
#include "DataProcessor.h"
#include "WorkerQueue.h"
#include "SharedMemoryCoordinator.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>

// Forward declarations
class RequestProcessor {
public:
    RequestProcessor(const std::string& nodeId);
    ~RequestProcessor();

    // For Process A (Leader)
    mini2::AggregatedResult ProcessRequestOnce(const mini2::Request& req);
    
    // For Team Leaders (B, E)
    void HandleTeamRequest(const mini2::Request& req);
    
    // For Workers (C, D, F)
    void HandleWorkerRequest(const mini2::Request& req);
    mini2::WorkerResult GenerateWorkerResult(const mini2::Request& req);
    
    // For Team Leaders - collect worker results
    void ReceiveWorkerResult(const mini2::WorkerResult& result);
    
    // Get aggregated results for a team
    mini2::AggregatedResult GetTeamAggregatedResult(const std::string& request_id);

    // Set neighbor connections from config
    void SetTeamLeaders(const std::vector<std::string>& team_leader_addresses);
    void SetWorkers(const std::vector<std::string>& worker_addresses);
    void SetLeaderAddress(const std::string& leader_address);
    
    // Real data processing
    void LoadDataset(const std::string& dataset_path);
    bool HasDataset() const { return data_processor_ != nullptr; }
    
    // Worker queue management (for workers C, F)
    void StartWorkerQueue(int num_threads = 2);
    void StopWorkerQueue();
    
    // Status and control
    mini2::StatusResponse GetStatus() const;
    std::string GetNodeState() const;
    void InitiateShutdown(int delay_seconds = 5);
    bool IsShuttingDown() const { return shutting_down_; }
    
    // Autonomous health check (public to allow configuration)
    void StartHealthCheckThread(int interval_seconds = 10);
    void StopHealthCheckThread();
    
    // Shared memory coordination (Phase 4)
    void InitializeSharedMemory(const std::string& segment_name, 
                                const std::vector<std::string>& member_ids);
    void UpdateSharedMemoryStatus();
    std::string FindLeastLoadedTeamLeader(const std::string& team);
    
    // Get current queue size for status reporting
    uint32_t GetQueueSize() const;

private:
    std::string node_id_;
    
    // gRPC client stubs for forwarding
    std::map<std::string, std::unique_ptr<mini2::TeamIngress::Stub>> team_leader_stubs_;
    std::map<std::string, std::unique_ptr<mini2::TeamIngress::Stub>> worker_stubs_;
    std::unique_ptr<mini2::TeamIngress::Stub> leader_stub_;
    
    // Health check stubs (NodeControl for Ping)
    std::map<std::string, std::unique_ptr<mini2::NodeControl::Stub>> health_check_stubs_;
    
    // Data processor for real data
    std::shared_ptr<DataProcessor> data_processor_;
    std::string current_dataset_path_;  // Track currently loaded dataset
    
    // Worker queue for non-blocking processing
    std::unique_ptr<WorkerQueue> worker_queue_;
    
    // Storage for results
    std::mutex results_mutex_;
    std::condition_variable results_cv_;
    std::map<std::string, std::vector<mini2::WorkerResult>> pending_results_;
    
    // Status tracking
    std::atomic<bool> shutting_down_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<int> requests_processed_;
    
    // Autonomous health check
    std::thread health_check_thread_;
    std::atomic<bool> health_check_running_;
    int health_check_interval_;
    
    void HealthCheckThreadFunc();
    
    // Shared memory coordination (Phase 4)
    std::unique_ptr<SharedMemoryCoordinator> shm_coordinator_;
    std::thread shm_update_thread_;
    std::atomic<bool> shm_update_running_;
    
    void SharedMemoryUpdateThreadFunc();
    
    // Helper methods
    int ForwardToTeamLeaders(const mini2::Request& req, bool need_green, bool need_pink);
    int ForwardToWorkers(const mini2::Request& req);
    mini2::WorkerResult GenerateMockData(const std::string& request_id, uint32_t part_index);
    mini2::WorkerResult ProcessRealData(const mini2::Request& req, size_t start_idx, size_t count);
    mini2::AggregatedResult CombineResults(const std::vector<mini2::WorkerResult>& results);
};
