#pragma once

#include <grpcpp/grpcpp.h>
#include "minitwo.grpc.pb.h"
#include "DataProcessor.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <utility>
#include <deque>

// Forward declarations
class RequestProcessor {
public:
    explicit RequestProcessor(const std::string& node_id);
    ~RequestProcessor();

    // For Process A (Leader)
    std::vector<mini2::WorkerResult> ProcessRequest(const mini2::Request& request);
    
    // For Team Leaders (B, E)
    void HandleTeamRequest(const mini2::Request& request);
    
    // For Workers (C, D, F)
    void HandleWorkerRequest(const mini2::Request& request);
    mini2::WorkerResult GenerateWorkerResult(const mini2::Request& request);
    mini2::WorkerResult ProcessTask(const mini2::Task& task, double& processing_time_ms);
    
    // For Team Leaders - collect worker results
    void ReceiveWorkerResult(const mini2::WorkerResult& result);

    // Set neighbor connections from config
    void SetTeamLeaders(const std::vector<std::pair<std::string, std::string>>& team_leader_endpoints);
    void SetWorkers(const std::map<std::string, std::pair<std::string, int>>& worker_info); // worker_id -> (addr, capacity_score)
    void SetLeaderAddress(const std::string& leader_address);
    
    // Real data processing
    void LoadDataset(const std::string& dataset_path);
    bool HasDataset() const;
    
    // Status and control
    mini2::StatusResponse GetStatus() const;
    std::string GetNodeState() const;
    void InitiateShutdown(int delay_seconds = 5);
    bool IsShuttingDown() const { return shutting_down_; }
    void MaintenanceTick();
    void UpdateWorkerHeartbeat(const std::string& worker_id, double recent_task_ms, uint32_t queue_len);
    mini2::Task RequestTaskForWorker(const std::string& worker_id);
    void EnsureWorkerRegistered(const std::string& worker_id);

private:
    std::string node_id_;
    
    // gRPC client stubs for forwarding
    std::map<std::string, std::unique_ptr<mini2::TeamIngress::Stub>> team_leader_stubs_;
    std::map<std::string, std::string> team_leader_roles_;
    std::map<std::string, std::unique_ptr<mini2::TeamIngress::Stub>> worker_stubs_;
    std::unique_ptr<mini2::TeamIngress::Stub> leader_stub_;
    
    // Data processor for real data
    std::shared_ptr<DataProcessor> data_processor_;
    std::string current_dataset_path_;  // Track currently loaded dataset
    mutable std::mutex dataset_mutex_;
    
    // Storage for results
    mutable std::mutex results_mutex_;
    std::condition_variable results_cv_;
    std::map<std::string, std::vector<mini2::WorkerResult>> pending_results_;
    
    // Status tracking
    std::atomic<bool> shutting_down_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<int> requests_processed_;
    
    // Worker stats and task queues for fault tolerance
    struct WorkerStats {
        std::string addr;
        uint32_t capacity_score = 1;
        double   avg_task_ms    = 0.0;
        size_t   queue_len      = 0;
        std::chrono::steady_clock::time_point last_heartbeat;
        bool     healthy        = true;
    };
    std::map<std::string, WorkerStats> worker_stats_;               // worker_id -> stats
    std::map<std::string, std::deque<mini2::Task>> worker_queues_;  // worker_id -> tasks
    std::deque<mini2::Task> team_task_queue_;                       // global team queue
    mutable std::mutex task_mutex_;
    
    // Helper methods
    double ComputeWorkerRank(const WorkerStats& ws) const;
    bool TryStealTask(const std::string& thief_id, mini2::Task& out_task);
    void OnWorkerBecameUnhealthy(const std::string& worker_id);
    std::string ChooseBestWorkerId() const;
    int ForwardToTeamLeaders(const mini2::Request& req, bool need_green, bool need_pink);
    int ForwardToWorkers(const mini2::Request& req);
    mini2::WorkerResult ProcessRealData(std::shared_ptr<DataProcessor> processor, const mini2::Request& req, size_t start_idx, size_t count);
    static grpc::ChannelArguments MakeLargeMessageArgs();
    void RegisterPeer(const std::string& addr,
                      std::map<std::string, std::unique_ptr<mini2::TeamIngress::Stub>>& target,
                      const char* label);
    void LoadDatasetIfNeeded(const mini2::Request& request);
    void ProcessLocally(std::shared_ptr<DataProcessor> processor, const mini2::Request& request, uint32_t partitions);
    
    std::shared_ptr<DataProcessor> GetDataProcessor() const;
};
