
#include <grpcpp/grpcpp.h>
#include "minitwo.grpc.pb.h"
#include "RequestProcessor.h"
#include "SessionManager.h"
#include "../common/logging.h"
#include <iostream>
#include <string>
#include <memory>
#include <thread>

using grpc::ServerContext;
using grpc::Status;

// Global shutdown flag
extern std::atomic<bool> g_shutdown_requested;

class NodeControlService final : public mini2::NodeControl::Service {
private:
    std::shared_ptr<RequestProcessor> processor_;
    std::string node_id_;
    
public:
    NodeControlService(std::shared_ptr<RequestProcessor> processor, const std::string& node_id)
        : processor_(processor), node_id_(node_id) {}
    
    Status Ping(ServerContext* ctx, const mini2::Heartbeat* req, mini2::HeartbeatAck* resp) override {
        LOG_DEBUG(node_id_, "NodeControl", 
                  "Ping from " + req->from() + " at " + std::to_string(req->ts_unix_ms()));
        
        // If this is a team leader receiving heartbeat from a worker, ensure worker is registered
        if (node_id_ == "B" || node_id_ == "E") {
            processor_->EnsureWorkerRegistered(req->from());
            if (req->recent_task_ms() > 0.0) {
                processor_->UpdateWorkerHeartbeat(req->from(), req->recent_task_ms(), req->queue_len());
            }
        }
        
        resp->set_ok(true);
        return Status::OK;
    }
    
    Status Broadcast(ServerContext*, const mini2::BroadcastMessage* req, mini2::HeartbeatAck* resp) override {
        std::cout << "[NodeControl:" << node_id_ << "] Broadcast from " << req->from_node() 
                  << " type: " << req->message_type() << std::endl;
        
        // Handle different broadcast types
        if (req->message_type() == "shutdown") {
            std::cout << "[NodeControl:" << node_id_ << "] Received shutdown broadcast" << std::endl;
            processor_->InitiateShutdown(3);  // 3 second delay
            g_shutdown_requested = true;
        } else if (req->message_type() == "status") {
            auto status = processor_->GetStatus();
            std::cout << "[NodeControl:" << node_id_ << "] Status: " << status.state() 
                      << " (queue=" << status.queue_size() << ")" << std::endl;
        }
        
        resp->set_ok(true);
        return Status::OK;
    }
    
    Status Shutdown(ServerContext*, const mini2::ShutdownRequest* req, mini2::ShutdownResponse* resp) override {
        std::cout << "[NodeControl:" << node_id_ << "] Shutdown request from " << req->from_node() 
                  << " with delay=" << req->delay_seconds() << "s" << std::endl;
        
        processor_->InitiateShutdown(req->delay_seconds());
        g_shutdown_requested = true;
        
        resp->set_acknowledged(true);
        resp->set_node_id(node_id_);
        return Status::OK;
    }
    
    Status GetStatus(ServerContext*, const mini2::StatusRequest* req, mini2::StatusResponse* resp) override {
        *resp = processor_->GetStatus();
        std::cout << "[NodeControl:" << node_id_ << "] Status request from " << req->from_node() 
                  << " - State: " << resp->state() << std::endl;
        return Status::OK;
    }
};

class TeamIngressService final : public mini2::TeamIngress::Service {
private:
    std::shared_ptr<RequestProcessor> processor_;
    std::string node_id_;
public:
    TeamIngressService(std::shared_ptr<RequestProcessor> processor, const std::string& node_id) 
        : processor_(processor), node_id_(node_id) {}
    
    Status HandleRequest(ServerContext* ctx, const mini2::Request* req, mini2::HeartbeatAck* resp) override {
        std::cout << "[TeamIngress] HandleRequest: " << req->request_id() 
                  << " (green=" << req->need_green() << ", pink=" << req->need_pink() << ")" << std::endl;
        
        // Determine if this is a team leader or worker
        if (node_id_ == "B" || node_id_ == "E") {
            LOG_INFO(node_id_, "TeamIngress",
                     "HandleRequest: received Request for team leader with request_id=" +
                     req->request_id() + " dataset=" + req->query());
            // Team leaders forward to workers or process locally
            processor_->HandleTeamRequest(*req);
        } else {
            // Workers process and send results back
            processor_->HandleWorkerRequest(*req);
        }
        
        resp->set_ok(true);
        return Status::OK;
    }
    
    Status PushWorkerResult(ServerContext* ctx, const mini2::WorkerResult* req, mini2::HeartbeatAck* resp) override {
        std::cout << "[TeamIngress] PushWorkerResult: " << req->request_id() 
                  << " part=" << req->part_index() << std::endl;
        
        // Store result in processor
        processor_->ReceiveWorkerResult(*req);
        
        resp->set_ok(true);
        return Status::OK;
    }
    
    Status RequestTask(ServerContext* ctx, const mini2::NodeId* req, mini2::Task* resp) override {
        LOG_DEBUG(node_id_, "TeamIngress", "RequestTask from " + req->id());
        
        // Only team leaders can assign tasks
        if (node_id_ == "B" || node_id_ == "E") {
            *resp = processor_->RequestTaskForWorker(req->id());
            if (!resp->request_id().empty()) {
                LOG_DEBUG(node_id_, "TeamIngress", 
                          "Assigned task " + resp->request_id() + "." + 
                          std::to_string(resp->chunk_id()) + " to " + req->id());
            }
        }
        // else return empty task (default constructed)
        
        return Status::OK;
    }
};

class ClientGatewayService final : public mini2::ClientGateway::Service {
private:
    std::shared_ptr<RequestProcessor> processor_;
    std::shared_ptr<SessionManager> session_manager_;
    
public:
    ClientGatewayService(std::shared_ptr<RequestProcessor> processor,
                         std::shared_ptr<SessionManager> session_mgr) 
        : processor_(processor), session_manager_(session_mgr) {}
    
    Status OpenSession(ServerContext*, const mini2::SessionOpen* req, mini2::HeartbeatAck* resp) override {
        std::cout << "[ClientGateway] OpenSession: " << req->request_id() << std::endl;
        resp->set_ok(true); 
        return Status::OK;
    }
    
    Status GetNext(ServerContext*, const mini2::NextChunkReq* req, mini2::NextChunkResp* resp) override {
        std::cout << "[ClientGateway] GetNext: " << req->request_id() 
                  << " index=" << req->next_index() << std::endl;
        
        bool success = session_manager_->GetNextChunk(req->request_id(), req->next_index(), resp);
        
        if (!success) {
            resp->set_has_more(false);
        }
        
        return Status::OK;
    }
    
    Status StartRequest(ServerContext*, const mini2::Request* req, mini2::SessionOpen* out) override {
        std::cout << "[ClientGateway] start: " << req->request_id() << std::endl;
        
        // Create session with unique ID
        std::string session_id = session_manager_->CreateSession(*req);
        out->set_request_id(session_id);
        out->set_accepted(true);
        out->set_status("QUEUED");
        out->set_timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        
        // Start background processing
        std::thread([this, session_id, req = *req]() {
            std::cout << "[ClientGateway] background processing for session " 
                      << session_id << std::endl;
            
            // Create a modified request with unique request_id (use session_id)
            // This ensures each concurrent client gets a unique request_id internally
            mini2::Request unique_req = req;
            unique_req.set_request_id(session_id);
            
            // Process request with unique request_id
            auto results = processor_->ProcessRequest(unique_req);
            
            // Add each chunk to session
            for (const auto& result : results) {
                mini2::WorkerResult wr;
                wr.set_request_id(session_id);
                wr.set_part_index(result.part_index());
                wr.set_payload(result.payload());
                session_manager_->AddChunk(session_id, wr);
            }
            
            // Mark session complete
            session_manager_->CompleteSession(session_id);
            
            std::cout << "[ClientGateway] background done for session " 
                      << session_id << std::endl;
        }).detach();
        
        return Status::OK;
    }

    Status PollNext(ServerContext*, const mini2::PollReq* req, mini2::PollResp* resp) override {
        std::cout << "[ClientGateway] PollNext: " << req->request_id() << std::endl;
        
        bool success = session_manager_->PollNextChunk(req->request_id(), resp);
        
        if (!success) {
            resp->set_ready(false);
            resp->set_has_more(false);
        }
        
        return Status::OK;
    }
    
    Status CloseSession(ServerContext*, const mini2::CloseSessionReq* req, mini2::CloseSessionResp* resp) override {
        std::cout << "[ClientGateway] CloseSession: " << req->session_id() << std::endl;
        
        session_manager_->CleanupSession(req->session_id());
        resp->set_success(true);
        
        return Status::OK;
    }
};
