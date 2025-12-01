#include <iostream>
#include <iomanip>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>


struct ProcessStatus {
    char process_id[8];
    enum State : uint32_t {
        IDLE = 0,
        BUSY = 1,
        SHUTDOWN = 2
    } state;
    uint32_t queue_size;
    int64_t last_update_ms;
    uint64_t memory_bytes;
    uint32_t requests_processed;
    uint32_t padding[2];
};

struct ShmSegmentData {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t max_processes;
    ProcessStatus processes[3];
    uint64_t segment_created_ms;
    uint32_t padding[10];
};

std::string StateToString(ProcessStatus::State state) {
    switch (state) {
        case ProcessStatus::IDLE: return "IDLE";
        case ProcessStatus::BUSY: return "BUSY";
        case ProcessStatus::SHUTDOWN: return "SHUTDOWN";
        default: return "UNKNOWN";
    }
}

std::string FormatMemory(uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes / (1024 * 1024)) + " MB";
}

int64_t GetCurrentTimeMs() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void InspectSegment(const std::string& segment_name) {
    std::string shm_name = "/" + segment_name;
    
    // Open shared memory
    int shm_fd = shm_open(shm_name.c_str(), O_RDONLY, 0666);
    if (shm_fd == -1) {
        std::cerr << "  Failed to open segment: " << segment_name 
                  << " (" << strerror(errno) << ")" << std::endl;
        return;
    }
    
    // Map the shared memory
    ShmSegmentData* segment = static_cast<ShmSegmentData*>(
        mmap(nullptr, sizeof(ShmSegmentData), PROT_READ, MAP_SHARED, shm_fd, 0)
    );
    
    if (segment == MAP_FAILED) {
        std::cerr << "  Failed to map segment: " << segment_name << std::endl;
        close(shm_fd);
        return;
    }
    
    // Display segment info
    std::cout << "\n  📊 Segment: " << segment_name << std::endl;
    std::cout << "  Magic: 0x" << std::hex << segment->magic << std::dec << std::endl;
    std::cout << "  Version: " << segment->version << std::endl;
    std::cout << "  Process count: " << segment->count << "/" << segment->max_processes << std::endl;
    
    int64_t current_time = GetCurrentTimeMs();
    
    // Display process statuses
    for (uint32_t i = 0; i < segment->count && i < segment->max_processes; i++) {
        const ProcessStatus& ps = segment->processes[i];
        std::cout << "\n  ├─ Process: " << std::string(ps.process_id) << std::endl;
        std::cout << "  │  State: " << StateToString(ps.state) << std::endl;
        std::cout << "  │  Queue size: " << ps.queue_size << std::endl;
        std::cout << "  │  Memory: " << FormatMemory(ps.memory_bytes) << std::endl;
        std::cout << "  │  Requests processed: " << ps.requests_processed << std::endl;
        
        int64_t age_ms = current_time - ps.last_update_ms;
        std::cout << "  │  Last update: " << (age_ms / 1000.0) << "s ago";
        if (age_ms > 30000) {
            std::cout << " [STALE]";
        }
        std::cout << std::endl;
    }
    
    // Cleanup
    munmap(segment, sizeof(ShmSegmentData));
    close(shm_fd);
}

int main(int argc, char* argv[]) {
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "  Phase 4: Shared Memory Inspector" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    
    if (argc > 1) {
        // Inspect specific segment
        for (int i = 1; i < argc; i++) {
            InspectSegment(argv[i]);
        }
    } else {
        // Inspect default segments
        std::cout << "\nInspecting default segments..." << std::endl;
        InspectSegment("shm_host1");
        InspectSegment("shm_host2");
    }
    
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "Usage: " << argv[0] << " [segment_name...]" << std::endl;
    std::cout << "  No args: Inspect shm_host1 and shm_host2" << std::endl;
    std::cout << "  With args: Inspect specified segments" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    
    return 0;
}
