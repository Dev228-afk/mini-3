# Mini-3 Logging System Implementation

## Overview
Mini-3 is based on Mini-2 with a new standardized logging infrastructure to support fault tolerance monitoring and debugging.

## Changes Made

### 1. New Logging Header (`src/cpp/common/logging.h`)
Created a lightweight, header-only logging system with:

**Features:**
- Timestamp formatting with millisecond precision (`YYYY-MM-DD HH:MM:SS.mmm`)
- Four log levels: DEBUG, INFO, WARN, ERROR
- Platform-independent (Windows/Unix time handling)
- Simple macro-based API
- Output to stderr for proper stream handling

**API:**
```cpp
LOG_INFO(node_id, component, message)
LOG_WARN(node_id, component, message)
LOG_ERROR(node_id, component, message)
LOG_DEBUG(node_id, component, message)  // Can be disabled with DISABLE_DEBUG_LOGS
```

**Output Format:**
```
<timestamp> <LEVEL> [<node>] [<component>] <message>
```

**Example:**
```
2024-12-04 19:45:12.345 INFO [A] [ServerMain] Node A listening at 0.0.0.0:50051 (public: 192.168.1.100:50051)
2024-12-04 19:45:22.456 INFO [A] [Heartbeat] alive #1 | state=IDLE | queue=0 | uptime=10s | requests=0
```

### 2. ServerMain.cpp Modifications
Replaced all high-level logging statements with the new logging system:

**Modified Components:**
- **Signal Handler:** Now logs shutdown signals with node context
- **Config Loading:** Success and failure messages use LOG_INFO/LOG_ERROR
- **Setup Messages:** All node configuration logging uses LOG_INFO
- **Server Lifecycle:** Startup and shutdown messages use LOG_INFO
- **Heartbeat Thread:** Periodic status updates use LOG_INFO with structured data

**Code Changes:**
- Added `#include "../common/logging.h"`
- Added `g_node_id` global for signal handler context
- Replaced ~15 `std::cout`/`std::cerr` statements with `LOG_*` macros
- Preserved user interaction prompt (`std::cout` for "Press Ctrl+C to stop")

**Unchanged:**
- No behavior changes to the server logic
- All error handling remains identical
- gRPC setup and configuration unchanged

### 3. Build System
No changes required to CMakeLists.txt - logging.h is header-only and included directly.

## Benefits

1. **Consistent Format:** All logs follow the same timestamp + level + context structure
2. **Easier Parsing:** Structured format enables log analysis tools
3. **Component Tracking:** Component tags help identify log sources
4. **Future Extensibility:** Easy to add file output, log levels filtering, or rotation
5. **Debugging Support:** DEBUG logs can be conditionally compiled out for production

## Testing
- ✅ Builds successfully on macOS with AppleClang 17
- ✅ All executables compile without warnings (except benign linker duplicates)
- ✅ Logging macros expand correctly with string concatenation

## Next Steps
Potential enhancements for fault tolerance work:
- Add log levels filtering at runtime
- Implement log file output alongside stderr
- Add structured logging for metrics (JSON format)
- Integrate with monitoring/alerting systems
- Add log rotation for long-running deployments
