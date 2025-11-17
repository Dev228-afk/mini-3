#include "SessionManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>

SessionManager::SessionManager() {
    std::cout << "[SessionManager] Initialized" << std::endl;
    StartCleanupThread();
}

SessionManager::~SessionManager() {
    StopCleanupThread();
    std::cout << "[SessionManager] Destroyed" << std::endl;
}

std::string SessionManager::GenerateSessionId() {
    // Generate unique session ID using timestamp + random
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::stringstream ss;
    ss << "session-" << ms << "-" << dis(gen);
    return ss.str();
}

std::string SessionManager::CreateSession(const mini2::Request& req) {
    std::string session_id = GenerateSessionId();
    
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    // Use emplace to construct in place (avoids copy/move of mutex)
    auto& session = sessions_[session_id];
    session.request_id = session_id;
    session.created_at = std::chrono::steady_clock::now();
    session.last_access = std::chrono::steady_clock::now();
    session.complete = false;
    session.next_poll_index = 0;
    
    std::cout << "[SessionManager] Created session: " << session_id 
              << " for query: " << req.query() << std::endl;
    
    return session_id;
}

void SessionManager::AddChunk(const std::string& session_id, const mini2::WorkerResult& result) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        std::cerr << "[SessionManager] Session not found: " << session_id << std::endl;
        return;
    }
    
    Session& session = it->second;
    std::lock_guard<std::mutex> session_lock(session.mutex);
    
    session.chunks.push_back(result);
    
    std::cout << "[SessionManager] Added chunk " << result.part_index() 
              << " to session " << session_id 
              << " (total chunks: " << session.chunks.size() << ")" << std::endl;
    
    // Notify waiting threads (for GetNext)
    session.cv.notify_all();
}

bool SessionManager::GetNextChunk(const std::string& session_id, uint32_t index, 
                                   mini2::NextChunkResp* resp) {
    std::unique_lock<std::mutex> lock(sessions_mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        std::cerr << "[SessionManager] GetNext: Session not found: " << session_id << std::endl;
        return false;
    }
    
    Session& session = it->second;
    session.last_access = std::chrono::steady_clock::now();  // Update access time
    lock.unlock();  // Release sessions_ lock
    
    std::unique_lock<std::mutex> session_lock(session.mutex);
    
        // Wait until chunk is available or session is complete
    while (index >= session.chunks.size() && !session.complete) {
        std::cout << "[SessionManager] GetNext: Waiting for chunk " << index 
                  << " in session " << session_id << std::endl;
        
        // Wait with timeout (185 seconds - longer than team leader timeout of 180s)
        if (session.cv.wait_for(session_lock, std::chrono::seconds(185)) == std::cv_status::timeout) {
            std::cerr << "[SessionManager] GetNext: Timeout waiting for chunk " << index << std::endl;
            return false;
        }
    }    // Check if chunk is available
    if (index < session.chunks.size()) {
        const auto& chunk = session.chunks[index];
        resp->set_request_id(session_id);
        resp->set_chunk(chunk.payload());
        
        // Check if more chunks are coming
        bool has_more = (index + 1 < session.chunks.size()) || !session.complete;
        resp->set_has_more(has_more);
        
        std::cout << "[SessionManager] GetNext: Returned chunk " << index 
                  << " (has_more=" << has_more << ")" << std::endl;
        
        return true;
    }
    
    // Session complete but no chunk at this index
    resp->set_request_id(session_id);
    resp->set_has_more(false);
    
    std::cout << "[SessionManager] GetNext: Session complete, no more chunks" << std::endl;
    return false;
}

bool SessionManager::PollNextChunk(const std::string& session_id, mini2::PollResp* resp) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        std::cerr << "[SessionManager] PollNext: Session not found: " << session_id << std::endl;
        return false;
    }
    
    Session& session = it->second;
    session.last_access = std::chrono::steady_clock::now();  // Update access time
    std::lock_guard<std::mutex> session_lock(session.mutex);
    
    resp->set_request_id(session_id);
    
    // Check if next chunk is available
    if (session.next_poll_index < session.chunks.size()) {
        const auto& chunk = session.chunks[session.next_poll_index];
        
        resp->set_ready(true);
        resp->set_chunk(chunk.payload());
        
        // Increment for next poll
        session.next_poll_index++;
        
        // Check if more chunks are coming
        bool has_more = (session.next_poll_index < session.chunks.size()) || !session.complete;
        resp->set_has_more(has_more);
        
        std::cout << "[SessionManager] PollNext: Returned chunk " 
                  << (session.next_poll_index - 1) 
                  << " (ready=true, has_more=" << has_more << ")" << std::endl;
        
        return true;
    }
    
    // Chunk not ready yet
    resp->set_ready(false);
    resp->set_has_more(!session.complete);
    
    std::cout << "[SessionManager] PollNext: Chunk not ready yet (complete=" 
              << session.complete << ")" << std::endl;
    
    return true;
}

void SessionManager::CompleteSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        std::cerr << "[SessionManager] CompleteSession: Session not found: " << session_id << std::endl;
        return;
    }
    
    Session& session = it->second;
    std::lock_guard<std::mutex> session_lock(session.mutex);
    
    session.complete = true;
    
    std::cout << "[SessionManager] Completed session: " << session_id 
              << " (total chunks: " << session.chunks.size() << ")" << std::endl;
    
    // Notify all waiting threads
    session.cv.notify_all();
}

void SessionManager::CleanupSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        sessions_.erase(it);
        std::cout << "[SessionManager] Cleaned up session: " << session_id << std::endl;
    }
}

void SessionManager::CleanupOldSessions(std::chrono::seconds max_age) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    int cleaned = 0;
    
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.created_at);
        
        if (age > max_age && it->second.complete) {
            it = sessions_.erase(it);
            cleaned++;
        } else {
            ++it;
        }
    }
    
    if (cleaned > 0) {
        std::cout << "[SessionManager] Cleaned up " << cleaned << " old sessions" << std::endl;
    }
}

void SessionManager::StartCleanupThread() {
    cleanup_running_ = true;
    cleanup_thread_ = std::thread(&SessionManager::CleanupThreadFunc, this);
    std::cout << "[SessionManager] Cleanup thread started (timeout: " 
              << session_timeout_.count() << " seconds)" << std::endl;
}

void SessionManager::StopCleanupThread() {
    cleanup_running_ = false;
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    std::cout << "[SessionManager] Cleanup thread stopped" << std::endl;
}

void SessionManager::CleanupThreadFunc() {
    while (cleanup_running_) {
        // Sleep for 60 seconds between cleanup runs
        for (int i = 0; i < 60 && cleanup_running_; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!cleanup_running_) break;
        
        CleanupStaleSessions();
    }
}

void SessionManager::CleanupStaleSessions() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    std::vector<std::string> to_remove;
    
    // Find stale sessions
    for (auto& pair : sessions_) {
        auto& session = pair.second;
        
        // Check if session is stale (no access for timeout duration)
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - session.last_access);
        
        if (elapsed > session_timeout_) {
            to_remove.push_back(pair.first);
        }
    }
    
    // Remove stale sessions
    for (const auto& session_id : to_remove) {
        sessions_.erase(session_id);
        std::cout << "[SessionManager] Removed stale session: " << session_id 
                  << " (idle for > " << session_timeout_.count() << " seconds)" << std::endl;
    }
    
    if (!to_remove.empty()) {
        std::cout << "[SessionManager] Cleanup complete: removed " 
                  << to_remove.size() << " stale session(s)" << std::endl;
    }
}
