#include <iostream>
#include <vector>
#include <iomanip>
#include <grpcpp/grpcpp.h>
#include "minitwo.grpc.pb.h"
#include "../common/MemoryTracker.h"
#include "../common/config.h"

// Simple tool to get total memory usage across all server nodes with breakdown (cross-platform)
// Usage: ./show_distributed_memory

int main() {
    // Load network configuration
    auto config = LoadConfig("../config/network_setup.json");
    
    std::vector<MemoryInfo> nodes;
    
    std::cout << "\n=== Distributed Memory Across All Nodes ===" << std::endl;
    
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
            std::cout << "  Node " << node.id << ": " 
                      << std::fixed << std::setprecision(2) 
                      << (resp.memory_bytes() / (1024.0 * 1024.0)) << " MB" << std::endl;
        } else {
            std::cout << "  Node " << node.id << ": Not running" << std::endl;
        }
    }
    
    // Calculate total
    if (nodes.empty()) {
        std::cout << "\nNo servers running!" << std::endl;
        return 0;
    }
    
    uint64_t total = CalculateTotalMemory(nodes);
    
    std::cout << "  " << std::string(38, '-') << std::endl;
    std::cout << "  Total: " << FormatMemoryMB(total) << std::endl;
    std::cout << "  Average: " << FormatMemoryMB(total / nodes.size()) << std::endl;
    std::cout << "==========================================\n" << std::endl;
    
    return 0;
}
