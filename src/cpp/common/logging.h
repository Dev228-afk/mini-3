#ifndef LOGGING_H
#define LOGGING_H

#include <iostream>
#include <string>
#include <ctime>
#include <chrono>
#include <sstream>
#include <iomanip>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

inline std::string NowTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now_time_t);
#else
    localtime_r(&now_time_t, &tm_buf);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << now_ms.count();
    return oss.str();
}

inline void LogInternal(LogLevel level, const std::string& node, 
                        const std::string& component, const std::string& msg) {
    std::string level_str;
    switch (level) {
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
        case LogLevel::INFO:  level_str = "INFO"; break;
        case LogLevel::WARN:  level_str = "WARN"; break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
    }
    
    std::cerr << NowTimestamp() << " " << level_str 
              << " [" << node << "] [" << component << "] " 
              << msg << std::endl;
}

#define LOG_INFO(node, component, msg) \
    LogInternal(LogLevel::INFO, node, component, msg)

#define LOG_WARN(node, component, msg) \
    LogInternal(LogLevel::WARN, node, component, msg)

#define LOG_ERROR(node, component, msg) \
    LogInternal(LogLevel::ERROR, node, component, msg)

#ifndef DISABLE_DEBUG_LOGS
#define LOG_DEBUG(node, component, msg) \
    LogInternal(LogLevel::DEBUG, node, component, msg)
#else
#define LOG_DEBUG(node, component, msg) do {} while(0)
#endif

#endif // LOGGING_H
