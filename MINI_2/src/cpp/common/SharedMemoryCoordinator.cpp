#include "SharedMemoryCoordinator.h"
#include <iostream>
#include <cstring>
#include <chrono>

// Platform-specific headers
#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

constexpr uint32_t SHARED_MEMORY_MAGIC = 0x534D454D; // "SMEM"
constexpr uint32_t SHARED_MEMORY_VERSION = 1;

SharedMemoryCoordinator::SharedMemoryCoordinator()
    : shm_fd_(-1), segment_(nullptr), initialized_(false), segment_size_(sizeof(ShmSegmentData)) {
}

SharedMemoryCoordinator::~SharedMemoryCoordinator() {
    Cleanup();
}

bool SharedMemoryCoordinator::Initialize(const std::string& segment_name, 
                                         const std::vector<std::string>& member_ids) {
    if (initialized_) {
        std::cerr << "[SharedMemory] Already initialized" << std::endl;
        return true;
    }
    
    if (member_ids.empty()) {
        std::cerr << "[SharedMemory] No member IDs provided" << std::endl;
        return false;
    }
    
    segment_name_ = segment_name;
    my_process_id_ = member_ids[0]; // First ID is this process
    
    std::cout << "[SharedMemory] Initializing segment: " << segment_name_ 
              << " for process: " << my_process_id_ << std::endl;
    
#ifdef _WIN32
    // Windows implementation (for completeness, but primarily using POSIX)
    std::cerr << "[SharedMemory] Windows not fully supported, use POSIX systems" << std::endl;
    return false;
#else
    // POSIX shared memory
    std::string shm_name = "/" + segment_name_;
    
    // Try to create or open existing segment
    shm_fd_ = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd_ == -1) {
        std::cerr << "[SharedMemory] Failed to create/open segment: " 
                  << strerror(errno) << std::endl;
        return false;
    }
    
    // Set the size of the shared memory object
    if (ftruncate(shm_fd_, segment_size_) == -1) {
        std::cerr << "[SharedMemory] Failed to set segment size: " 
                  << strerror(errno) << std::endl;
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    // Map the shared memory
    segment_ = static_cast<ShmSegmentData*>(
        mmap(nullptr, segment_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0)
    );
    
    if (segment_ == MAP_FAILED) {
        std::cerr << "[SharedMemory] Failed to map segment: " 
                  << strerror(errno) << std::endl;
        close(shm_fd_);
        shm_fd_ = -1;
        segment_ = nullptr;
        return false;
    }
    
    // Initialize segment data if this is the first process
    InitializeSegmentData();
    
    // Add this process to the segment
    UpdateStatus(ProcessStatus::IDLE, 0, 0);
    
    initialized_ = true;
    std::cout << "[SharedMemory] Successfully initialized segment: " 
              << segment_name_ << std::endl;
    return true;
#endif
}

void SharedMemoryCoordinator::InitializeSegmentData() {
    if (!segment_) return;
    
    // Check if already initialized by another process
    if (segment_->magic == SHARED_MEMORY_MAGIC && 
        segment_->version == SHARED_MEMORY_VERSION) {
        std::cout << "[SharedMemory] Segment already initialized by another process" << std::endl;
        return;
    }
    
    // Initialize the segment
    std::cout << "[SharedMemory] Initializing new segment data" << std::endl;
    memset(segment_, 0, segment_size_);
    segment_->magic = SHARED_MEMORY_MAGIC;
    segment_->version = SHARED_MEMORY_VERSION;
    segment_->count = 0;
    segment_->max_processes = 3;
    segment_->segment_created_ms = GetCurrentTimeMs();
}

void SharedMemoryCoordinator::UpdateStatus(ProcessStatus::State state, 
                                           uint32_t queue_size, 
                                           uint64_t memory_bytes) {
    if (!initialized_ || !segment_) return;
    
    std::lock_guard<std::mutex> lock(local_mutex_);
    
    int index = FindOrAddProcess(my_process_id_);
    if (index < 0) {
        std::cerr << "[SharedMemory] Failed to find/add process: " 
                  << my_process_id_ << std::endl;
        return;
    }
    
    ProcessStatus& ps = segment_->processes[index];
    ps.state = state;
    ps.queue_size = queue_size;
    ps.memory_bytes = memory_bytes;
    ps.last_update_ms = GetCurrentTimeMs();
    ps.requests_processed++;
}

ProcessStatus SharedMemoryCoordinator::GetStatus(const std::string& process_id) const {
    if (!initialized_ || !segment_) {
        return ProcessStatus{};
    }
    
    std::lock_guard<std::mutex> lock(local_mutex_);
    
    int index = FindProcessIndex(process_id);
    if (index >= 0) {
        return segment_->processes[index];
    }
    
    return ProcessStatus{};
}

std::vector<ProcessStatus> SharedMemoryCoordinator::GetAllStatuses() const {
    std::vector<ProcessStatus> statuses;
    
    if (!initialized_ || !segment_) {
        return statuses;
    }
    
    std::lock_guard<std::mutex> lock(local_mutex_);
    
    for (uint32_t i = 0; i < segment_->count && i < segment_->max_processes; i++) {
        statuses.push_back(segment_->processes[i]);
    }
    
    return statuses;
}

std::string SharedMemoryCoordinator::FindLeastLoadedProcess() const {
    if (!initialized_ || !segment_) {
        return "";
    }
    
    std::lock_guard<std::mutex> lock(local_mutex_);
    
    std::string best_process;
    uint32_t min_queue_size = UINT32_MAX;
    ProcessStatus::State best_state = ProcessStatus::SHUTDOWN;
    
    int64_t current_time = GetCurrentTimeMs();
    
    for (uint32_t i = 0; i < segment_->count && i < segment_->max_processes; i++) {
        const ProcessStatus& ps = segment_->processes[i];
        
        // Skip if process is shutdown or stale (no update in last 30 seconds)
        if (ps.state == ProcessStatus::SHUTDOWN || 
            (current_time - ps.last_update_ms) > 30000) {
            continue;
        }
        
        // Prefer IDLE over BUSY
        if (ps.state == ProcessStatus::IDLE) {
            if (best_state != ProcessStatus::IDLE || ps.queue_size < min_queue_size) {
                best_process = std::string(ps.process_id);
                min_queue_size = ps.queue_size;
                best_state = ps.state;
            }
        } else if (best_state != ProcessStatus::IDLE && ps.queue_size < min_queue_size) {
            best_process = std::string(ps.process_id);
            min_queue_size = ps.queue_size;
            best_state = ps.state;
        }
    }
    
    return best_process;
}

void SharedMemoryCoordinator::Cleanup() {
    if (!initialized_) return;
    
    std::cout << "[SharedMemory] Cleaning up segment: " << segment_name_ << std::endl;
    
    // Mark this process as shutdown
    if (segment_) {
        UpdateStatus(ProcessStatus::SHUTDOWN, 0, 0);
    }
    
#ifndef _WIN32
    if (segment_ && segment_ != MAP_FAILED) {
        munmap(segment_, segment_size_);
        segment_ = nullptr;
    }
    
    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    
    // Optionally unlink the shared memory (only if you want to remove it)
    // Note: This will remove it for all processes, so typically we don't do this
    // std::string shm_name = "/" + segment_name_;
    // shm_unlink(shm_name.c_str());
#endif
    
    initialized_ = false;
}

int64_t SharedMemoryCoordinator::GetCurrentTimeMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int SharedMemoryCoordinator::FindProcessIndex(const std::string& process_id) const {
    if (!segment_) return -1;
    
    for (uint32_t i = 0; i < segment_->count && i < segment_->max_processes; i++) {
        if (std::string(segment_->processes[i].process_id) == process_id) {
            return static_cast<int>(i);
        }
    }
    
    return -1;
}

int SharedMemoryCoordinator::FindOrAddProcess(const std::string& process_id) {
    if (!segment_) return -1;
    
    // First, try to find existing
    int index = FindProcessIndex(process_id);
    if (index >= 0) {
        return index;
    }
    
    // Add new process if there's room
    if (segment_->count < segment_->max_processes) {
        index = segment_->count++;
        ProcessStatus& ps = segment_->processes[index];
        memset(&ps, 0, sizeof(ProcessStatus));
        strncpy(ps.process_id, process_id.c_str(), sizeof(ps.process_id) - 1);
        ps.process_id[sizeof(ps.process_id) - 1] = '\0';
        ps.state = ProcessStatus::IDLE;
        ps.last_update_ms = GetCurrentTimeMs();
        
        std::cout << "[SharedMemory] Added new process: " << process_id 
                  << " at index " << index << std::endl;
        return index;
    }
    
    std::cerr << "[SharedMemory] Segment full, cannot add process: " 
              << process_id << std::endl;
    return -1;
}
