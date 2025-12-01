
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

std::atomic<bool> g_shutdown_requested(false);

void SignalHandler(int signal) {
    std::cout << "\n[Server] Received signal " << signal << ", initiating graceful shutdown..." << std::endl;
    g_shutdown_requested = true;
}

int main(int argc, char** argv){
    // basic signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    std::string config_path = "config/network_setup.json";
    std::string node_id = "A";
    
    if (argc > 1 && argv[1][0] != '-') {
        node_id = argv[1];
    } else {
        for (int i=1;i<argc;i++){
            std::string a = argv[i];
            if (a=="--config" && i+1<argc) config_path = argv[++i];
            else if (a=="--node" && i+1<argc) node_id = argv[++i];
        }
    }
    
    std::vector<std::string> config_paths = {
        config_path,
        "../config/network_setup.json",
        "../../config/network_setup.json",
        "../../../config/network_setup.json"
    };
    
    NetworkConfig cfg;
    bool config_loaded = false;
    for (const auto& path : config_paths) {
        try {
            cfg = LoadConfig(path);
            if (!cfg.nodes.empty()) {
                config_loaded = true;
                std::cout << "[Server] Loaded config from: " << path << std::endl;
                break;
            }
        } catch (...) { continue; }
    }
    
    if (!config_loaded) {
        std::cerr << "[Server] FATAL: Could not load config from any path. Server cannot start." << std::endl;
        return 1;
    }
    
    auto it = cfg.nodes.find(node_id);
    if (it==cfg.nodes.end()){ std::cerr<<"Unknown node "<<node_id<<"\n"; return 1; }
    const auto& me = it->second;
    
    std::string bind_addr = "0.0.0.0:" + std::to_string(me.port);
    std::string public_addr = me.host + ":" + std::to_string(me.port);

    auto processor = std::make_shared<RequestProcessor>(node_id);
    auto session_manager = std::make_shared<SessionManager>();    
    if (node_id == "A") {
        std::string addr_B = cfg.nodes["B"].host + ":" + std::to_string(cfg.nodes["B"].port);
        std::string addr_E = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);
        processor->SetTeamLeaders({
            {"green", addr_B},
            {"pink", addr_E}
        });
        std::cout << "[Setup] Node A configured as Leader with team leaders: " << addr_B << ", " << addr_E << "\n";
    } else if (node_id == "B") {
        std::string addr_A = cfg.nodes["A"].host + ":" + std::to_string(cfg.nodes["A"].port);
        std::string addr_C = cfg.nodes["C"].host + ":" + std::to_string(cfg.nodes["C"].port);
        processor->SetLeaderAddress(addr_A);
        std::vector<std::string> workers = {addr_C};
        processor->SetWorkers(workers);
        std::cout << "[Setup] B = green team leader (A=" << addr_A << ", C=" << addr_C << ")\n";
        std::cout << "[Setup] dataset path comes from Request.query\n";
    } else if (node_id == "E") {
        std::string addr_A = cfg.nodes["A"].host + ":" + std::to_string(cfg.nodes["A"].port);
        std::string addr_D = cfg.nodes["D"].host + ":" + std::to_string(cfg.nodes["D"].port);
        std::string addr_F = cfg.nodes["F"].host + ":" + std::to_string(cfg.nodes["F"].port);
        processor->SetLeaderAddress(addr_A);
        std::vector<std::string> workers = {addr_D, addr_F};
        processor->SetWorkers(workers);
        std::cout << "[Setup] E = pink team leader (A=" << addr_A << ", D=" << addr_D << ", F=" << addr_F << ")\n";
        std::cout << "[Setup] dataset path comes from Request.query\n";
    } else if (node_id == "C" || node_id == "D" || node_id == "F") {
        std::string team_leader_addr;
        if (node_id == "C") {
            team_leader_addr = cfg.nodes["B"].host + ":" + std::to_string(cfg.nodes["B"].port);
            std::cout << "[Setup] C = worker (green team, leader=" << team_leader_addr << ")\n";
        } else if (node_id == "D") {
            team_leader_addr = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);
            std::cout << "[Setup] D = worker (pink team, leader=" << team_leader_addr << ")\n";
        } else if (node_id == "F") {
            team_leader_addr = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);
            std::cout << "[Setup] F = worker (pink team, leader=" << team_leader_addr << ")\n";
        }
        
        processor->SetLeaderAddress(team_leader_addr);
        std::cout << "[Setup] dataset path comes from Request.query\n";
    }

    grpc::ServerBuilder b;
    
    b.SetMaxReceiveMessageSize(1536 * 1024 * 1024); // 1.5GB
    b.SetMaxSendMessageSize(1536 * 1024 * 1024);    // 1.5GB
    
    NodeControlService nodeSvc(processor, node_id);
    TeamIngressService teamSvc(processor, node_id);
    ClientGatewayService clientSvc(processor, session_manager);

    b.AddListeningPort(bind_addr, grpc::InsecureServerCredentials());
    b.RegisterService(&nodeSvc);
    b.RegisterService(&teamSvc);
    if (node_id=="A") b.RegisterService(&clientSvc);

    std::unique_ptr<grpc::Server> server(b.BuildAndStart());
    std::cout << "Node " << node_id << " listening at " << bind_addr << " (public: " << public_addr << ")" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    while (!g_shutdown_requested && !processor->IsShuttingDown()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\n[Server:" << node_id << "] Initiating graceful shutdown..." << std::endl;
    
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    server->Shutdown(deadline);
    
    std::cout << "[Server:" << node_id << "] Shutdown complete" << std::endl;
    
    return 0;
}
