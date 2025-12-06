
#include <grpcpp/grpcpp.h>
#include "minitwo.grpc.pb.h"
#include "../common/config.h"
#include "../common/logging.h"
#include "RequestProcessor.h"
#include "SessionManager.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <sstream>

#include "Handlers.cpp"

std::atomic<bool> g_shutdown_requested(false);
std::string g_node_id = "unknown"; // Set in main()

void SignalHandler(int signal) {
    LOG_INFO(g_node_id, "Signal", "Received signal " + std::to_string(signal) + ", initiating graceful shutdown...");
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
    
    g_node_id = node_id; // Set global for signal handler
    
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
                LOG_INFO("startup", "ServerMain", "Loaded config from: " + path);
                break;
            }
        } catch (...) { continue; }
    }
    
    if (!config_loaded) {
        LOG_ERROR("startup", "ServerMain", "FATAL: Could not load config from any path. Server cannot start.");
        return 1;
    }
    
    auto it = cfg.nodes.find(node_id);
    if (it==cfg.nodes.end()){ 
        LOG_ERROR(node_id, "ServerMain", "Unknown node: " + node_id);
        return 1; 
    }
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
        LOG_INFO(node_id, "ServerMain", "Node A configured as Leader with team leaders: " + addr_B + ", " + addr_E);
    } else if (node_id == "B" || node_id == "E") {
        std::string addr_A = cfg.nodes["A"].host + ":" + std::to_string(cfg.nodes["A"].port);
        processor->SetLeaderAddress(addr_A);

        // Build worker list from config based on team membership
        // Include any node in same team that might pull tasks (C, D, F)
        std::map<std::string, std::pair<std::string, int>> workers;
        for (const auto& [id, info] : cfg.nodes) {
            if (id == node_id) continue;                 // skip self
            if (info.role == "LEADER") continue;         // skip gateway leader
            if (info.team != me.team) continue;          // only same team
            // Include WORKER nodes and any TEAM_LEADER that pulls tasks (like D from E)
            if (id == "C" || id == "D" || id == "F") {   // nodes that pull tasks
                std::string addr = info.host + ":" + std::to_string(info.port);
                workers[id] = {addr, info.capacity_score};
            }
        }

        processor->SetWorkers(workers);

        // Log configuration
        std::ostringstream oss;
        bool first = true;
        for (const auto& [id, pair] : workers) {
            if (!first) oss << ", ";
            oss << id << "=" << pair.first;
            first = false;
        }
        LOG_INFO(node_id, "ServerMain", node_id + " team leader (A=" + addr_A + ", workers=" + oss.str() + ")");
        LOG_INFO(node_id, "ServerMain", "dataset path comes from Request.query");
    } else if (node_id == "C" || node_id == "D" || node_id == "F") {
        std::string team_leader_addr;
        if (node_id == "C") {
            team_leader_addr = cfg.nodes["B"].host + ":" + std::to_string(cfg.nodes["B"].port);
            LOG_INFO(node_id, "ServerMain", "C = worker (green team, leader=" + team_leader_addr + ")");
        } else if (node_id == "D") {
            team_leader_addr = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);
            LOG_INFO(node_id, "ServerMain", "D = worker (pink team, leader=" + team_leader_addr + ")");
        } else if (node_id == "F") {
            team_leader_addr = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);
            LOG_INFO(node_id, "ServerMain", "F = worker (pink team, leader=" + team_leader_addr + ")");
        }
        
        processor->SetLeaderAddress(team_leader_addr);
        LOG_INFO(node_id, "ServerMain", "dataset path comes from Request.query");
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
    LOG_INFO(node_id, "ServerMain", "Node " + node_id + " listening at " + bind_addr + " (public: " + public_addr + ")");
    LOG_INFO(node_id, "ServerMain", "Press Ctrl+C to stop");
    
    // Start worker task pulling thread for worker nodes
    std::thread worker_thread;
    std::thread worker_heartbeat_thread;
    std::atomic<double> last_task_ms(0.0);
    if (node_id == "C" || node_id == "D" || node_id == "F") {
        worker_thread = std::thread([&]() {
            // Get team leader stub
            std::string team_leader_addr;
            if (node_id == "C") {
                team_leader_addr = cfg.nodes["B"].host + ":" + std::to_string(cfg.nodes["B"].port);
            } else {
                team_leader_addr = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);
            }
            
            auto channel = grpc::CreateChannel(team_leader_addr, grpc::InsecureChannelCredentials());
            auto stub = mini2::TeamIngress::NewStub(channel);
            
            LOG_INFO(node_id, "WorkerLoop", "Starting task pulling loop");
            
            while (!g_shutdown_requested) {
                // Request a task from team leader
                mini2::NodeId req;
                req.set_id(node_id);
                mini2::Task task;
                grpc::ClientContext ctx;
                
                grpc::Status status = stub->RequestTask(&ctx, req, &task);
                
                if (status.ok() && !task.request_id().empty()) {
                    LOG_DEBUG(node_id, "WorkerLoop", 
                              "Pulled task " + task.request_id() + "." + std::to_string(task.chunk_id()));
                    
                    // Process the task
                    double processing_ms = 0.0;
                    auto result = processor->ProcessTask(task, processing_ms);
                    last_task_ms.store(processing_ms);
                    
                    LOG_DEBUG(node_id, "WorkerLoop", 
                              "Finished task " + task.request_id() + "." + std::to_string(task.chunk_id()) + 
                              " in " + std::to_string(processing_ms) + "ms");
                    
                    // Send result back to team leader
                    grpc::ClientContext result_ctx;
                    mini2::HeartbeatAck ack;
                    status = stub->PushWorkerResult(&result_ctx, result, &ack);
                    
                    if (!status.ok()) {
                        LOG_ERROR(node_id, "WorkerLoop", 
                                  "Failed to push result: " + status.error_message());
                    }
                } else {
                    // No task available, backoff
                    if (!status.ok()) {
                        LOG_DEBUG(node_id, "WorkerLoop", 
                                  "RequestTask failed: " + status.error_message());
                    } else {
                        LOG_DEBUG(node_id, "WorkerLoop", "No tasks available");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            
            LOG_INFO(node_id, "WorkerLoop", "Stopping task pulling loop");
        });
        
        // Start worker heartbeat sending thread
        worker_heartbeat_thread = std::thread([&]() {
            std::string team_leader_addr;
            if (node_id == "C") {
                team_leader_addr = cfg.nodes["B"].host + ":" + std::to_string(cfg.nodes["B"].port);
            } else {
                team_leader_addr = cfg.nodes["E"].host + ":" + std::to_string(cfg.nodes["E"].port);
            }
            
            auto channel = grpc::CreateChannel(team_leader_addr, grpc::InsecureChannelCredentials());
            auto node_ctrl_stub = mini2::NodeControl::NewStub(channel);
            
            LOG_INFO(node_id, "WorkerHeartbeat", "Starting heartbeat thread");
            
            while (!g_shutdown_requested) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (g_shutdown_requested) break;
                
                // Send heartbeat with metrics
                mini2::Heartbeat hb;
                hb.set_from(node_id);
                hb.set_ts_unix_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                hb.set_recent_task_ms(last_task_ms.load());
                hb.set_queue_len(0); // Workers process one task at a time
                hb.set_capacity_score(cfg.nodes[node_id].capacity_score);
                
                grpc::ClientContext ctx;
                mini2::HeartbeatAck ack;
                grpc::Status status = node_ctrl_stub->Ping(&ctx, hb, &ack);
                
                if (!status.ok()) {
                    LOG_DEBUG(node_id, "WorkerHeartbeat", 
                              "Failed to send heartbeat: " + status.error_message());
                }
            }
            
            LOG_INFO(node_id, "WorkerHeartbeat", "Stopping heartbeat thread");
        });
    }
    
    // Start periodic heartbeat logging thread
    std::atomic<bool> heartbeat_running(true);
    std::thread heartbeat_thread([&]() {
        int counter = 0;
        while (heartbeat_running && !g_shutdown_requested) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (!heartbeat_running || g_shutdown_requested) break;
            
            counter++;
            auto status = processor->GetStatus();
            std::ostringstream oss;
            oss << "alive #" << counter 
                << " | state=" << status.state()
                << " | queue=" << status.queue_size()
                << " | uptime=" << status.uptime_seconds() << "s"
                << " | requests=" << status.requests_processed();
            LOG_INFO(node_id, "Heartbeat", oss.str());
        }
    });
    
    // Start maintenance thread for team leaders
    std::thread maintenance_thread;
    if (node_id == "B" || node_id == "E") {
        maintenance_thread = std::thread([&]() {
            LOG_INFO(node_id, "Maintenance", "Starting maintenance thread");
            
            while (!g_shutdown_requested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (g_shutdown_requested) break;
                
                processor->MaintenanceTick();
            }
            
            LOG_INFO(node_id, "Maintenance", "Stopping maintenance thread");
        });
    }
    
    while (!g_shutdown_requested && !processor->IsShuttingDown()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO(node_id, "ServerMain", "Initiating graceful shutdown...");
    
    // Stop heartbeat thread
    heartbeat_running = false;
    if (heartbeat_thread.joinable()) {
        heartbeat_thread.join();
    }
    
    // Stop maintenance thread
    if (maintenance_thread.joinable()) {
        maintenance_thread.join();
    }
    
    // Stop worker thread
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    
    // Stop worker heartbeat thread
    if (worker_heartbeat_thread.joinable()) {
        worker_heartbeat_thread.join();
    }
    
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    server->Shutdown(deadline);
    
    LOG_INFO(node_id, "ServerMain", "Shutdown complete");
    
    return 0;
}
