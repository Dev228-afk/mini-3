#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <mutex>

// Process status structure for shared memory
struct ProcessStatus {
    char process_id[8];      // Node ID (A, B, C, D, E, F)
    enum State : uint32_t {
        IDLE = 0,
        BUSY = 1,
        SHUTDOWN = 2
    } state;
    uint32_t queue_size;     // Number of pending requests
    int64_t last_update_ms;  // Timestamp in milliseconds
    uint64_t memory_bytes;   // Memory usage in bytes
    uint32_t requests_processed; // Total requests completed
    uint32_t padding[2];     // Padding for alignment
};

// Shared memory segment structure (data layout)
struct ShmSegmentData {
    uint32_t magic;          // Magic number for validation (0x534D454D = "SMEM")
    uint32_t version;        // Version number
    uint32_t count;          // Number of active processes
    uint32_t max_processes;  // Maximum processes (3)
    ProcessStatus processes[3]; // Fixed array for up to 3 processes
    uint64_t segment_created_ms; // When segment was created
    uint32_t padding[10];    // Reserved for future use
};

// Shared memory coordinator class
class SharedMemoryCoordinator {
public:
    SharedMemoryCoordinator();
    ~SharedMemoryCoordinator();
    
    // Initialize shared memory segment
    bool Initialize(const std::string& segment_name, const std::vector<std::string>& member_ids);
    
    // Update this process's status
    void UpdateStatus(ProcessStatus::State state, uint32_t queue_size, uint64_t memory_bytes = 0);
    
    // Read status of another process
    ProcessStatus GetStatus(const std::string& process_id) const;
    
    // Get all statuses in this segment
    std::vector<ProcessStatus> GetAllStatuses() const;
    
    // Find the least loaded process in this segment
    std::string FindLeastLoadedProcess() const;
    
    // Check if initialized
    bool IsInitialized() const { return initialized_; }
    
    // Get segment name
    std::string GetSegmentName() const { return segment_name_; }
    
    // Cleanup (called on shutdown)
    void Cleanup();

private:
    std::string segment_name_;
    std::string my_process_id_;
    int shm_fd_;
    ShmSegmentData* segment_;
    bool initialized_;
    size_t segment_size_;
    
    // Mutex for thread-safe access (local only, not in shared memory)
    mutable std::mutex local_mutex_;
    
    // Helper methods
    int64_t GetCurrentTimeMs() const;
    int FindProcessIndex(const std::string& process_id) const;
    int FindOrAddProcess(const std::string& process_id);
    void InitializeSegmentData();
};
