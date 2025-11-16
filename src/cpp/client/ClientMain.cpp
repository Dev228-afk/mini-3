
#include <grpcpp/grpcpp.h>
#include "minitwo.grpc.pb.h"
#include "../common/MemoryTracker.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <thread>
#include <vector>

// Helper to create channel with increased message size limits (1.5GB for very large datasets)
std::shared_ptr<grpc::Channel> CreateChannelWithLimits(const std::string& target) {
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(1536 * 1024 * 1024); // 1.5GB
    args.SetMaxSendMessageSize(1536 * 1024 * 1024);    // 1.5GB
    return grpc::CreateCustomChannel(target, grpc::InsecureChannelCredentials(), args);
}

void testPing(const std::string& target) {
    auto channel = CreateChannelWithLimits(target);
    std::unique_ptr<mini2::NodeControl::Stub> stub = mini2::NodeControl::NewStub(channel);

    std::cout << "Testing Ping to " << target << "... ";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    grpc::ClientContext ctx;
    mini2::Heartbeat hb;
    hb.set_from("client");
    hb.set_ts_unix_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    mini2::HeartbeatAck ack;
    auto status = stub->Ping(&ctx, hb, &ack);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (!status.ok()) {
        std::cout << "FAILED: " << status.error_message() << std::endl;
    } else {
        std::cout << "SUCCESS (RTT: " << std::fixed << std::setprecision(2) 
                  << duration.count() / 1000.0 << " ms(milliseconds))" << std::endl;
    }
}

// Simple test for ClientGateway's OpenSession RPC
void testOpenSession(const std::string& target) {
    auto channel = CreateChannelWithLimits(target);
    std::unique_ptr<mini2::ClientGateway::Stub> stub = mini2::ClientGateway::NewStub(channel);

    std::cout << "Testing OpenSession to " << target << "... ";
    
    grpc::ClientContext ctx;
    mini2::SessionOpen open; 
    open.set_request_id("smoke-test");
    mini2::HeartbeatAck ack;
    
    auto status = stub->OpenSession(&ctx, open, &ack);
    
    if (!status.ok()) {
        std::cout << "FAILED: " << status.error_message() << std::endl;
    } else {
        std::cout << "SUCCESS (ok=" << ack.ok() << ")" << std::endl;
    }
}

// Example: Calculate total distributed memory from all nodes
// NOTE: This is just an example showing the pattern. In real usage, you would:
// 1. Query each running server node to get its actual memory via RPC
// 2. Collect the MemoryInfo from each node
// 3. Use CalculateTotalMemory() to sum them up
void printDistributedMemory(const std::vector<std::string>& node_targets) {
    std::vector<MemoryInfo> nodes;
    
    // Query each node for its actual memory
    for (const auto& target : node_targets) {
        // Extract node ID from target (e.g., "localhost:50051" -> "A")
        std::string node_id = target.substr(target.find_last_of(':') + 1);
        
        // Get actual memory from this process (if querying a remote node, 
        // you'd make an RPC call to that node's server)
        uint64_t memory = GetProcessMemory();
        
        nodes.push_back({memory, node_id});
    }
    
    // Calculate total
    uint64_t total = CalculateTotalMemory(nodes);
    
    std::cout << "\n=== Distributed Memory Summary ===" << std::endl;
    for (const auto& node : nodes) {
        std::cout << "Node " << node.node_id << ": " 
                  << FormatMemoryMB(node.rss_bytes) << std::endl;
    }
    std::cout << "Total: " << FormatMemoryMB(total) << std::endl;
    std::cout << "Average: " << FormatMemoryMB(total / nodes.size()) << std::endl;
    std::cout << "==================================\n" << std::endl;
}

// Strategy B: GetNext (sequential pull)
void testStrategyB_GetNext(const std::string& gateway, const std::string& dataset_path = "") {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Testing Strategy B: GetNext (Sequential)" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    auto channel = CreateChannelWithLimits(gateway);
    std::unique_ptr<mini2::ClientGateway::Stub> stub = mini2::ClientGateway::NewStub(channel);
    
    // Start request
    std::cout << "Step 1: Starting session..." << std::endl;
    grpc::ClientContext ctx1;
    mini2::Request req;
    req.set_request_id("test-strategyB-getnext");
    req.set_query(dataset_path.empty() ? "mock_data" : dataset_path);
    req.set_need_green(true);
    req.set_need_pink(true);
    
    mini2::SessionOpen session;
    auto start_session = std::chrono::high_resolution_clock::now();
    auto status = stub->StartRequest(&ctx1, req, &session);
    auto end_session = std::chrono::high_resolution_clock::now();
    auto session_latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_session - start_session);
    
    if (!status.ok()) {
        std::cerr << "FAILED: StartRequest - " << status.error_message() << std::endl;
        return;
    }
    
    std::cout << "Session started: " << session.request_id() << std::endl;
    std::cout << "  Session creation time: " << session_latency.count() << " ms" << std::endl;
    std::cout << std::endl;
    
    // Get chunks one by one
    std::cout << "Step 2: Retrieving chunks sequentially..." << std::endl;
    uint32_t index = 0;
    uint64_t total_bytes = 0;
    auto start_chunks = std::chrono::high_resolution_clock::now();
    auto first_chunk_time = std::chrono::high_resolution_clock::time_point();
    
    while (true) {
        grpc::ClientContext ctx2;
        mini2::NextChunkReq next_req;
        next_req.set_request_id(session.request_id());
        next_req.set_next_index(index);
        
        mini2::NextChunkResp resp;
        auto start_chunk = std::chrono::high_resolution_clock::now();
        status = stub->GetNext(&ctx2, next_req, &resp);
        auto end_chunk = std::chrono::high_resolution_clock::now();
        
        if (index == 0) {
            first_chunk_time = end_chunk;
        }
        
        if (!status.ok()) {
            std::cerr << "✗ GetNext failed: " << status.error_message() << std::endl;
            break;
        }
        
        if (!resp.has_more() && resp.chunk().empty()) {
            std::cout << "No more chunks available" << std::endl;
            break;
        }
        
        total_bytes += resp.chunk().size();
        auto chunk_latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_chunk - start_chunk);
        
        std::cout << "  ✓ Chunk " << index 
                  << ": " << resp.chunk().size() << " bytes"
                  << " (latency: " << chunk_latency.count() << " ms)"
                  << " (has_more: " << (resp.has_more() ? "yes" : "no") << ")" << std::endl;
        
        index++;
        
        if (!resp.has_more()) {
            break;
        }
    }
    
    auto end_chunks = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_chunks - start_session);
    auto time_to_first_chunk = std::chrono::duration_cast<std::chrono::milliseconds>(first_chunk_time - start_session);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Strategy B (GetNext) Results:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total chunks: " << index << std::endl;
    std::cout << "Total bytes: " << total_bytes << std::endl;
    std::cout << "Time to first chunk: " << time_to_first_chunk.count() << " ms ⚡" << std::endl;
    std::cout << "Total time: " << total_time.count() << " ms" << std::endl;
    std::cout << "RPC calls made: " << (1 + index) << " (1 StartRequest + " << index << " GetNext)" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

// Strategy B: PollNext (polling)
void testStrategyB_PollNext(const std::string& gateway, const std::string& dataset_path = "") {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Testing Strategy B: PollNext (Polling)" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    auto channel = CreateChannelWithLimits(gateway);
    std::unique_ptr<mini2::ClientGateway::Stub> stub = mini2::ClientGateway::NewStub(channel);
    
    // Start request
    std::cout << "Step 1: Starting session..." << std::endl;
    grpc::ClientContext ctx1;
    mini2::Request req;
    req.set_request_id("test-strategyB-pollnext");
    req.set_query(dataset_path.empty() ? "mock_data" : dataset_path);
    req.set_need_green(true);
    req.set_need_pink(true);
    
    mini2::SessionOpen session;
    auto start_session = std::chrono::high_resolution_clock::now();
    auto status = stub->StartRequest(&ctx1, req, &session);
    
    if (!status.ok()) {
        std::cerr << "✗ StartRequest failed: " << status.error_message() << std::endl;
        return;
    }
    
    std::cout << "✓ Session started: " << session.request_id() << std::endl;
    std::cout << std::endl;
    
    // Poll for chunks
    std::cout << "Step 2: Polling for chunks..." << std::endl;
    int chunks_received = 0;
    uint64_t total_bytes = 0;
    int poll_count = 0;
    auto first_chunk_time = std::chrono::high_resolution_clock::time_point();
    
    while (true) {
        grpc::ClientContext ctx2;
        mini2::PollReq poll_req;
        poll_req.set_request_id(session.request_id());
        
        mini2::PollResp resp;
        status = stub->PollNext(&ctx2, poll_req, &resp);
        poll_count++;
        
        if (!status.ok()) {
            std::cerr << "✗ PollNext failed: " << status.error_message() << std::endl;
            break;
        }
        
        if (resp.ready()) {
            if (chunks_received == 0) {
                first_chunk_time = std::chrono::high_resolution_clock::now();
            }
            
            total_bytes += resp.chunk().size();
            chunks_received++;
            
            std::cout << "  ✓ Chunk " << chunks_received 
                      << ": " << resp.chunk().size() << " bytes"
                      << " (has_more: " << (resp.has_more() ? "yes" : "no") << ")" << std::endl;
        } else {
            std::cout << "  ⏳ Not ready yet, polling again... (attempt " << poll_count << ")" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Wait a bit before next poll
        }
        
        if (!resp.has_more()) {
            break;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_session);
    auto time_to_first_chunk = std::chrono::duration_cast<std::chrono::milliseconds>(first_chunk_time - start_session);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Strategy B (PollNext) Results:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total chunks: " << chunks_received << std::endl;
    std::cout << "Total bytes: " << total_bytes << std::endl;
    std::cout << "Time to first chunk: " << time_to_first_chunk.count() << " ms ⚡" << std::endl;
    std::cout << "Total time: " << total_time.count() << " ms" << std::endl;
    std::cout << "RPC calls made: " << (1 + poll_count) << " (1 StartRequest + " << poll_count << " PollNext)" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

int main(int argc, char** argv){
    std::string gateway = "localhost:50050";
    std::string mode = "session";
    std::string dataset_path = "";  // Dataset path for query field
    
    for (int i=1;i<argc;i++){
        std::string a = argv[i];
        if ((a=="--gateway" || a=="--server") && i+1<argc) gateway = argv[++i];
        else if (a=="--mode" && i+1<argc) mode = argv[++i];
        else if (a=="--dataset" && i+1<argc) dataset_path = argv[++i];
    }
    
    std::cout << "=== Mini2 Client ===" << std::endl;
    std::cout << "Gateway: " << gateway << std::endl;
    std::cout << "Mode: " << mode << std::endl;
    if (!dataset_path.empty()) {
        std::cout << "Dataset: " << dataset_path << std::endl;
    }
    std::cout << std::endl;
    
    if (mode == "ping") {
        testPing(gateway);
    } else if (mode == "session") {
        testOpenSession(gateway);
    } else if (mode == "all") {
        // Test all 6 processes
        std::cout << "Testing all processes:" << std::endl;
        testPing("localhost:50050");  // A
        testPing("localhost:50051");  // B
        testPing("localhost:50052");  // C
        testPing("localhost:50053");  // D
        testPing("localhost:50054");  // E
        testPing("localhost:50055");  // F
    } else if (mode == "request") {
        // Test Phase 2: Full request processing
        std::cout << "Testing RequestOnce (Phase 2):" << std::endl;
        
        auto channel = CreateChannelWithLimits(gateway);
        std::unique_ptr<mini2::ClientGateway::Stub> stub = mini2::ClientGateway::NewStub(channel);
        
        // Test 1: Request Green team only
        {
            std::cout << "\n=== Test 1: Green Team Only ===" << std::endl;
            grpc::ClientContext ctx;
            mini2::Request req;
            req.set_request_id("test-green-001");
            req.set_query(dataset_path.empty() ? "mock_data" : dataset_path);
            req.set_need_green(true);
            req.set_need_pink(false);
            
            mini2::AggregatedResult result;
            auto start = std::chrono::high_resolution_clock::now();
            auto status = stub->RequestOnce(&ctx, req, &result);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            if (status.ok()) {
                std::cout << "Request ID: " << result.request_id() << std::endl;
                std::cout << "  Total Rows: " << result.total_rows() << std::endl;
                std::cout << "  Total Bytes: " << result.total_bytes() << std::endl;
                std::cout << "  Chunks: " << result.chunks_size() << std::endl;
                std::cout << "  Latency: " << duration.count() << " ms" << std::endl;
            } else {
                std::cout << "FAILED: " << status.error_message() << std::endl;
            }
        }
        
        // Test 2: Request Pink team only
        {
            std::cout << "\n=== Test 2: Pink Team Only ===" << std::endl;
            grpc::ClientContext ctx;
            mini2::Request req;
            req.set_request_id("test-pink-001");
            req.set_query(dataset_path.empty() ? "mock_data" : dataset_path);
            req.set_need_green(false);
            req.set_need_pink(true);
            
            mini2::AggregatedResult result;
            auto start = std::chrono::high_resolution_clock::now();
            auto status = stub->RequestOnce(&ctx, req, &result);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            if (status.ok()) {
                std::cout << "Request ID: " << result.request_id() << std::endl;
                std::cout << "  Total Rows: " << result.total_rows() << std::endl;
                std::cout << "  Total Bytes: " << result.total_bytes() << std::endl;
                std::cout << "  Chunks: " << result.chunks_size() << std::endl;
                std::cout << "  Latency: " << duration.count() << " ms" << std::endl;
            } else {
                std::cout << "FAILED: " << status.error_message() << std::endl;
            }
        }
        
        // Test 3: Request both teams
        {
            std::cout << "\n=== Test 3: Both Teams ===" << std::endl;
            grpc::ClientContext ctx;
            mini2::Request req;
            req.set_request_id("test-both-001");
            req.set_query(dataset_path.empty() ? "mock_data" : dataset_path);
            req.set_need_green(true);
            req.set_need_pink(true);
            
            mini2::AggregatedResult result;
            auto start = std::chrono::high_resolution_clock::now();
            auto status = stub->RequestOnce(&ctx, req, &result);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            if (status.ok()) {
                std::cout << "Request ID: " << result.request_id() << std::endl;
                std::cout << "  Total Rows: " << result.total_rows() << std::endl;
                std::cout << "  Total Bytes: " << result.total_bytes() << std::endl;
                std::cout << "  Chunks: " << result.chunks_size() << std::endl;
                std::cout << "  Latency: " << duration.count() << " ms" << std::endl;
            } else {
                std::cout << "FAILED: " << status.error_message() << std::endl;
            }
        }
    } else if (mode == "strategy-b-getnext") {
        // Test Phase 3: Strategy B with GetNext
        testStrategyB_GetNext(gateway, dataset_path);
    } else if (mode == "strategy-b-pollnext") {
        // Test Phase 3: Strategy B with PollNext
        testStrategyB_PollNext(gateway, dataset_path);
    } else if (mode == "phase3") {
        // Test Phase 3: Compare all strategies
        std::cout << "\n############################################" << std::endl;
        std::cout << "### Phase 3: Chunking Strategies Test ###" << std::endl;
        std::cout << "############################################\n" << std::endl;
        
        auto channel = CreateChannelWithLimits(gateway);
        std::unique_ptr<mini2::ClientGateway::Stub> stub = mini2::ClientGateway::NewStub(channel);
        
        // Strategy A: RequestOnce
        std::cout << "\n=== Strategy A: RequestOnce (All at Once) ===" << std::endl;
        {
            grpc::ClientContext ctx;
            mini2::Request req;
            req.set_request_id("test-strategyA");
            req.set_query("SELECT * FROM data");
            req.set_need_green(true);
            req.set_need_pink(true);
            
            mini2::AggregatedResult result;
            auto start = std::chrono::high_resolution_clock::now();
            auto status = stub->RequestOnce(&ctx, req, &result);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            if (status.ok()) {
                std::cout << "✓ Total Rows: " << result.total_rows() << std::endl;
                std::cout << "  Total Bytes: " << result.total_bytes() << std::endl;
                std::cout << "  Chunks: " << result.chunks_size() << std::endl;
                std::cout << "  Time to first chunk: N/A (all at once)" << std::endl;
                std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
                std::cout << "  RPC calls: 1" << std::endl;
            } else {
                std::cout << "✗ FAILED: " << status.error_message() << std::endl;
            }
        }
        
        // Strategy B: GetNext
        testStrategyB_GetNext(gateway);
        
        // Strategy B: PollNext
        testStrategyB_PollNext(gateway);
        
        std::cout << "\n############################################" << std::endl;
        std::cout << "### Phase 3 Testing Complete! ###" << std::endl;
        std::cout << "############################################\n" << std::endl;
    } else {
        std::cout << "Unknown mode: " << mode << std::endl;
        std::cout << "Available modes: ping, session, all, request, strategy-b-getnext, strategy-b-pollnext, phase3" << std::endl;
        return 1;
    }
    
    return 0;
}
