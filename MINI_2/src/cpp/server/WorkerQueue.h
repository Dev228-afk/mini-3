#pragma once

#include "minitwo.grpc.pb.h"
#include "DataProcessor.h"
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <functional>

// Callback type for completed work
using WorkCompletedCallback = std::function<void(const mini2::WorkerResult&)>;

class WorkerQueue {
public:
    WorkerQueue(const std::string& node_id, int num_threads = 2);
    ~WorkerQueue();
    
    // Start processing threads
    void Start();
    
    // Stop processing (graceful)
    void Stop();
    
    // Enqueue a request (non-blocking)
    void EnqueueRequest(const mini2::Request& req, WorkCompletedCallback callback);
    
    // Set data processor for real data
    void SetDataProcessor(std::shared_ptr<DataProcessor> processor);
    
    // Get queue status
    size_t GetQueueSize() const;
    bool IsIdle() const;
    int GetProcessedCount() const { return requests_processed_; }

private:
    struct WorkItem {
        mini2::Request request;
        WorkCompletedCallback callback;
        std::chrono::steady_clock::time_point enqueue_time;
    };
    
    std::string node_id_;
    int num_threads_;
    std::vector<std::thread> worker_threads_;
    std::queue<WorkItem> work_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_;
    std::atomic<int> active_workers_;
    std::atomic<int> requests_processed_;
    
    std::shared_ptr<DataProcessor> data_processor_;
    
    // Worker thread function
    void WorkerThreadFunc(int thread_id);
    
    // Process a single request
    mini2::WorkerResult ProcessRequest(const mini2::Request& req);
};
