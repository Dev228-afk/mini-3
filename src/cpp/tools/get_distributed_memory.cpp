#include <iostream>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "minitwo.grpc.pb.h"
#include "../common/MemoryTracker.h"
#include "../common/config.h"

// Simple tool to get total memory usage across all server nodes (cross-platform)
// Usage: ./get_distributed_memory

int main() {
    // Load network configuration
    auto config = LoadConfig("../config/network_setup.json");
    
    std::vector<MemoryInfo> nodes;
    
    // Query each node via RPC to get its memory
    for (const auto& pair : config.nodes) {
        const auto& node = pair.second;
        std::string target = node.host + ":" + std::to_string(node.port);
        
        // Create gRPC channel and stub
        auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
        auto stub = mini2::NodeControl::NewStub(channel);
        
        // Call GetStatus RPC
        grpc::ClientContext ctx;
        mini2::StatusRequest req;
        req.set_from_node("memory_monitor");
        mini2::StatusResponse resp;
        
        auto status = stub->GetStatus(&ctx, req, &resp);
        
        if (status.ok() && resp.memory_bytes() > 0) {
            nodes.push_back({resp.memory_bytes(), node.id});
        }
    }
    
    // Calculate total
    if (nodes.empty()) {
        std::cout << "0" << std::endl;
        return 0;
    }
    
    uint64_t total = CalculateTotalMemory(nodes);
    
    // Output just the total in MB for easy parsing by script
    std::cout << (total / (1024 * 1024)) << std::endl;
    
    return 0;
}
