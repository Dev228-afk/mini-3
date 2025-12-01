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
    
    // For Team Leaders - collect worker results
    void ReceiveWorkerResult(const mini2::WorkerResult& result);

    // Set neighbor connections from config
    void SetTeamLeaders(const std::vector<std::pair<std::string, std::string>>& team_leader_endpoints);
    void SetWorkers(const std::vector<std::string>& worker_addresses);
    void SetLeaderAddress(const std::string& leader_address);
    
    // Real data processing
    void LoadDataset(const std::string& dataset_path);
    bool HasDataset() const;
    
    // Status and control
    mini2::StatusResponse GetStatus() const;
    std::string GetNodeState() const;
    void InitiateShutdown(int delay_seconds = 5);
    bool IsShuttingDown() const { return shutting_down_; }

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
    
    // Helper methods
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
