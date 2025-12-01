#pragma once

#include "minitwo.grpc.pb.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <thread>

class SessionManager {
public:
    SessionManager();
    ~SessionManager();
    
    // Create new session for a request
    std::string CreateSession(const mini2::Request& req);
    
    // Add chunk to session (called as results arrive from workers)
    void AddChunk(const std::string& session_id, const mini2::WorkerResult& result);
    
    // Get next chunk by index (blocking - waits if chunk not ready yet)
    bool GetNextChunk(const std::string& session_id, uint32_t index, 
                      mini2::NextChunkResp* resp);
    
    // Poll for next available chunk (non-blocking)
    bool PollNextChunk(const std::string& session_id, mini2::PollResp* resp);
    
    // Mark session as complete (no more chunks coming)
    void CompleteSession(const std::string& session_id);
    
    // Cleanup session data
    void CleanupSession(const std::string& session_id);
    
    // Cleanup old sessions (background task)
    void CleanupOldSessions(std::chrono::seconds max_age);
    
    // Start/stop automatic cleanup thread
    void StartCleanupThread();
    void StopCleanupThread();

private:
    struct Session {
        std::string request_id;
        std::vector<mini2::WorkerResult> chunks;
        bool complete = false;
        uint32_t next_poll_index = 0;  // For PollNext tracking
        std::chrono::steady_clock::time_point created_at;
        std::chrono::steady_clock::time_point last_access;  // Track last access for timeout
        std::mutex mutex;
        std::condition_variable cv;  // For blocking GetNext
    };
    
    std::map<std::string, Session> sessions_;
    std::mutex sessions_mutex_;
    
    // Cleanup thread management
    std::thread cleanup_thread_;
    bool cleanup_running_ = false;
    std::chrono::seconds session_timeout_{300};  // 5 minutes default
    
    // Generate unique session ID
    std::string GenerateSessionId();
    
    // Background cleanup thread function
    void CleanupThreadFunc();
    
    // Clean up stale sessions (called by cleanup thread)
    void CleanupStaleSessions();
};
