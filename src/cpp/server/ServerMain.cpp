
#include <grpcpp/grpcpp.h>
#include "minitwo.grpc.pb.h"
#include "../common/config.h"
#include "RequestProcessor.h"
#include "SessionManager.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>

#include "Handlers.cpp"

// Global shutdown flag for signal handling
std::atomic<bool> g_shutdown_requested(false);

void SignalHandler(int signal) {
    std::cout << "\n[Server] Received signal " << signal << ", initiating graceful shutdown..." << std::endl;
    g_shutdown_requested = true;
}

int main(int argc, char** argv){
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, SignalHandler);   // Ctrl+C
    signal(SIGTERM, SignalHandler);  // kill command
    
    std::string config_path = "config/network_setup.json";
    std::string node_id = "A";
    
    // Check if first arg is just node ID (without --node flag)
    if (argc > 1 && argv[1][0] != '-') {
        node_id = argv[1];
    } else {
        for (int i=1;i<argc;i++){
            std::string a = argv[i];
            if (a=="--config" && i+1<argc) config_path = argv[++i];
            else if (a=="--node" && i+1<argc) node_id = argv[++i];
        }
    }
    
    auto cfg = LoadConfig(config_path);
    auto it = cfg.nodes.find(node_id);
    if (it==cfg.nodes.end()){ std::cerr<<"Unknown node "<<node_id<<"\n"; return 1; }
    const auto& me = it->second;
    std::string addr = me.host + ":" + std::to_string(me.port);

    // Create RequestProcessor and configure connections based on role
    auto processor = std::make_shared<RequestProcessor>(node_id);
    auto session_manager = std::make_shared<SessionManager>();
    
    // Setup connections based on node role - using config addresses
    if (node_id == "A") {
        // Process A: Connect to team leaders B and E
        std::string addr_B = cfg.nodes["B"].host + ":" + std::to_string(cfg.nodes["B"].port);
        std::string addr_E = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);
        std::vector<std::string> team_leaders = {addr_B, addr_E};
        processor->SetTeamLeaders(team_leaders);
        std::cout << "[Setup] Node A configured as Leader with team leaders: " << addr_B << ", " << addr_E << "\n";
    } else if (node_id == "B") {
        // Team Leader B (Green Team): Connect back to A and to worker C
        std::string addr_A = cfg.nodes["A"].host + ":" + std::to_string(cfg.nodes["A"].port);
        std::string addr_C = cfg.nodes["C"].host + ":" + std::to_string(cfg.nodes["C"].port);
        processor->SetLeaderAddress(addr_A);
        std::vector<std::string> workers = {addr_C};
        processor->SetWorkers(workers);
        std::cout << "[Setup] Node B configured as Green Team Leader (connects to A: " << addr_A << ", worker C: " << addr_C << ")\n";
        std::cout << "[Setup] Dataset will be loaded from Request.query field on demand\n";
    } else if (node_id == "E") {
        // Team Leader E (Pink Team): Connect back to A and to workers D, F
        std::string addr_A = cfg.nodes["A"].host + ":" + std::to_string(cfg.nodes["A"].port);
        std::string addr_D = cfg.nodes["D"].host + ":" + std::to_string(cfg.nodes["D"].port);
        std::string addr_F = cfg.nodes["F"].host + ":" + std::to_string(cfg.nodes["F"].port);
        processor->SetLeaderAddress(addr_A);
        std::vector<std::string> workers = {addr_D, addr_F};
        processor->SetWorkers(workers);
        std::cout << "[Setup] Node E configured as Pink Team Leader (connects to A: " << addr_A << ", workers D: " << addr_D << ", F: " << addr_F << ")\n";
        std::cout << "[Setup] Dataset will be loaded from Request.query field on demand\n";
    } else if (node_id == "C" || node_id == "D" || node_id == "F") {
        // Workers: Connect to their respective team leaders and start worker queue
        std::string team_leader_addr;
        if (node_id == "C") {
            team_leader_addr = cfg.nodes["B"].host + ":" + std::to_string(cfg.nodes["B"].port);  // Connect to B
            std::cout << "[Setup] Node C configured as Worker (Green Team, leader: " << team_leader_addr << ")\n";
        } else if (node_id == "D") {
            team_leader_addr = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);  // Connect to E
            std::cout << "[Setup] Node D configured as Worker (Pink Team, leader: " << team_leader_addr << ")\n";
        } else if (node_id == "F") {
            team_leader_addr = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);  // Connect to E
            std::cout << "[Setup] Node F configured as Worker (Pink Team, leader: " << team_leader_addr << ")\n";
        }
        
        processor->SetLeaderAddress(team_leader_addr);
        
        // Start worker queue with 2 threads for non-blocking processing
        processor->StartWorkerQueue(2);
        std::cout << "[Setup] Started worker queue for non-blocking processing\n";
        std::cout << "[Setup] Dataset will be loaded from Request.query field on demand\n";
    }

    grpc::ServerBuilder b;
    
    // Increase message size limits to handle very large datasets (1.5GB)
    b.SetMaxReceiveMessageSize(1536 * 1024 * 1024); // 1.5GB
    b.SetMaxSendMessageSize(1536 * 1024 * 1024);    // 1.5GB
    
    NodeControlService nodeSvc(processor, node_id);
    TeamIngressService teamSvc(processor, node_id);
    ClientGatewayService clientSvc(processor, session_manager);

    b.AddListeningPort(addr, grpc::InsecureServerCredentials());
    b.RegisterService(&nodeSvc);
    b.RegisterService(&teamSvc);
    if (node_id=="A") b.RegisterService(&clientSvc);

    std::unique_ptr<grpc::Server> server(b.BuildAndStart());
    std::cout << "Node " << node_id << " listening at " << addr << std::endl;
    std::cout << "Press Ctrl+C for graceful shutdown" << std::endl;
    
    // ========================================================================
    // Phase 4: Initialize Shared Memory Coordination
    // ========================================================================
    for (const auto& seg : cfg.segments) {
        // Check if this node is a member of this segment
        auto it = std::find(seg.members.begin(), seg.members.end(), node_id);
        if (it != seg.members.end()) {
            std::cout << "[Server:" << node_id << "] Found in shared memory segment: " 
                      << seg.name << std::endl;
            
            // Create member_ids vector with this node first, then others
            std::vector<std::string> member_ids;
            member_ids.push_back(node_id); // This node goes first
            for (const auto& member : seg.members) {
                if (member != node_id) {
                    member_ids.push_back(member);
                }
            }
            
            // Initialize shared memory
            processor->InitializeSharedMemory(seg.name, member_ids);
            break; // Only join one segment
        }
    }
    
    // Start autonomous health check thread if enabled in config
    // Default: enabled with 10 second interval (student-level implementation)
    processor->StartHealthCheckThread(10);
    
    // Wait for shutdown signal instead of blocking indefinitely
    while (!g_shutdown_requested && !processor->IsShuttingDown()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Graceful shutdown
    std::cout << "\n[Server:" << node_id << "] Initiating graceful shutdown..." << std::endl;
    
    // Give server 5 seconds to finish pending requests
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    server->Shutdown(deadline);
    
    std::cout << "[Server:" << node_id << "] Shutdown complete" << std::endl;
    
    return 0;
}
