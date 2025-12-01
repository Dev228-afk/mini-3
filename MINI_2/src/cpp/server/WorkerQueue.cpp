#include "WorkerQueue.h"
#include <iostream>
#include <sstream>

WorkerQueue::WorkerQueue(const std::string& node_id, int num_threads)
    : node_id_(node_id)
    , num_threads_(num_threads)
    , running_(false)
    , active_workers_(0)
    , requests_processed_(0) {
}

WorkerQueue::~WorkerQueue() {
    Stop();
}

void WorkerQueue::Start() {
    if (running_) return;
    
    running_ = true;
    std::cout << "[WorkerQueue:" << node_id_ << "] Starting " << num_threads_ << " worker threads" << std::endl;
    
    for (int i = 0; i < num_threads_; ++i) {
        worker_threads_.emplace_back(&WorkerQueue::WorkerThreadFunc, this, i);
    }
}

void WorkerQueue::Stop() {
    if (!running_) return;
    
    std::cout << "[WorkerQueue:" << node_id_ << "] Stopping worker threads..." << std::endl;
    running_ = false;
    queue_cv_.notify_all();
    
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    worker_threads_.clear();
    std::cout << "[WorkerQueue:" << node_id_ << "] All worker threads stopped. Processed " 
              << requests_processed_ << " requests." << std::endl;
}

void WorkerQueue::EnqueueRequest(const mini2::Request& req, WorkCompletedCallback callback) {
    WorkItem item;
    item.request = req;
    item.callback = callback;
    item.enqueue_time = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        work_queue_.push(std::move(item));
    }
    
    queue_cv_.notify_one();
    
    std::cout << "[WorkerQueue:" << node_id_ << "] Enqueued request: " << req.request_id() 
              << " (queue size: " << GetQueueSize() << ")" << std::endl;
}

void WorkerQueue::SetDataProcessor(std::shared_ptr<DataProcessor> processor) {
    data_processor_ = processor;
}

size_t WorkerQueue::GetQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return work_queue_.size();
}

bool WorkerQueue::IsIdle() const {
    return GetQueueSize() == 0 && active_workers_ == 0;
}

void WorkerQueue::WorkerThreadFunc(int thread_id) {
    std::cout << "[WorkerQueue:" << node_id_ << "] Thread " << thread_id << " started" << std::endl;
    
    while (running_) {
        WorkItem item;
        
        // Wait for work
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return !work_queue_.empty() || !running_; 
            });
            
            if (!running_ && work_queue_.empty()) {
                break;
            }
            
            if (work_queue_.empty()) {
                continue;
            }
            
            item = std::move(work_queue_.front());
            work_queue_.pop();
        }
        
        // Process work
        active_workers_++;
        
        auto start_time = std::chrono::steady_clock::now();
        auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            start_time - item.enqueue_time).count();
        
        std::cout << "[WorkerQueue:" << node_id_ << "][Thread " << thread_id << "] "
                  << "Processing request: " << item.request.request_id() 
                  << " (waited " << wait_time << "ms)" << std::endl;
        
        mini2::WorkerResult result = ProcessRequest(item.request);
        
        auto end_time = std::chrono::steady_clock::now();
        auto process_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        std::cout << "[WorkerQueue:" << node_id_ << "][Thread " << thread_id << "] "
                  << "Completed request: " << item.request.request_id() 
                  << " (took " << process_time << "ms)" << std::endl;
        
        requests_processed_++;
        active_workers_--;
        
        // Invoke callback with result
        if (item.callback) {
            item.callback(result);
        }
    }
    
    std::cout << "[WorkerQueue:" << node_id_ << "] Thread " << thread_id << " stopped" << std::endl;
}

mini2::WorkerResult WorkerQueue::ProcessRequest(const mini2::Request& req) {
    mini2::WorkerResult result;
    result.set_request_id(req.request_id());
    result.set_part_index(0);
    
    // If we have a data processor and query is a file path, process real data
    if (data_processor_ && !req.query().empty()) {
        try {
            // Get all data (or chunk based on team assignment)
            size_t total_rows = data_processor_->GetTotalRows();
            auto chunk = data_processor_->GetChunk(0, total_rows);
            
            // Serialize chunk to bytes
            std::ostringstream oss;
            oss << data_processor_->GetHeader() << "\n";
            for (const auto& row : chunk) {
                oss << row.GetRaw() << "\n";
            }
            
            std::string data = oss.str();
            result.set_payload(data);
            
            std::cout << "[WorkerQueue:" << node_id_ << "] Processed " << chunk.size() 
                      << " rows (" << data.size() << " bytes)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[WorkerQueue:" << node_id_ << "] Error processing data: " << e.what() << std::endl;
            result.set_payload("ERROR: " + std::string(e.what()));
        }
    } else {
        // No data loaded or empty query
        result.set_payload("");
    }
    
    return result;
}
