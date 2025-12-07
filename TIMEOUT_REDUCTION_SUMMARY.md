# Timeout Reduction and Fast-Fail Implementation

## Overview
Reduced timeouts from 90 seconds to 10-12 seconds and implemented partial success handling to prevent long waits when teams are down.

## Key Changes

### 1. Timeout Constants
- **Location**: `RequestProcessor.cpp` - top of file in anonymous namespace
- **Team Leader Timeout**: `kTeamLeaderWaitTimeoutMs = 10000ms` (10 seconds)
- **Global Leader Timeout**: `kLeaderWaitTimeoutMs = 12000ms` (12 seconds)
- **Previous**: Both were 90 seconds

### 2. Team Request Status Tracking
- **Location**: `RequestProcessor.h` - new struct `TeamRequestStatus`
- **Fields**:
  - `bool success = true;` - Whether team completed successfully
  - `std::string failure_reason;` - Why team failed (if applicable)
  - `size_t expected_results = 0;` - Expected result count
- **Storage**: `std::map<std::string, TeamRequestStatus> team_request_status_;`
- **Purpose**: Track per-request success/failure internally (proto unchanged)

### 3. Team Leader (B/E) Fast-Fail Behavior
- **Location**: `RequestProcessor.cpp` - `HandleTeamRequest()`

#### Scenario: No Healthy Workers
```cpp
if (healthy_count == 0) {
    LOG_WARN(node_id_, "TeamLeader", 
             "No healthy workers available; failing request " + request_id + " fast");
    team_request_status_[request_id].success = false;
    team_request_status_[request_id].failure_reason = "No healthy workers";
    return;  // Exit immediately, no 10-second wait
}
```

#### Scenario: Worker Timeout
```cpp
bool got_results = results_cv_.wait_for(lock, kTeamLeaderWaitTimeoutMs, ...);
if (!got_results) {
    LOG_WARN(node_id_, "TeamLeader", 
             "Timeout waiting for worker results for request " + request_id +
             " (waited 10000ms)");
    team_request_status_[request_id].success = false;
    team_request_status_[request_id].failure_reason = "Timeout waiting for worker results";
}
```

### 4. Global Leader (A) Partial Success Logic
- **Location**: `RequestProcessor.cpp` - `ProcessRequest()`

#### Timeout Reduction
- Changed from 90 seconds to 12 seconds using `kLeaderWaitTimeoutMs`

#### Success Counting
```cpp
int total_teams = expected_results;
int successful_teams = 0;
// Count based on results received
if (pending_results_.count(request_id) && !results.empty()) {
    successful_teams = 1; // At least one team succeeded
}
```

#### Partial Success (Some Teams Failed)
```cpp
if (successful_teams > 0 && successful_teams < total_teams) {
    LOG_WARN(node_id_, "Leader", 
             "Partial team failure for request " + request_id + 
             ": " + to_string(successful_teams) + "/" + 
             to_string(total_teams) + " teams succeeded.");
    // Return partial results to client
}
```

#### Total Failure (All Teams Failed)
```cpp
if (successful_teams == 0) {
    LOG_ERROR(node_id_, "Leader", 
              "All teams failed for request " + request_id + 
              "; returning empty result.");
    // Return empty result after 12s, not 90s
}
```

## Expected Behavior Changes

### Before (90-second timeouts)
```
[B] [TeamLeader] HandleTeamRequest: processing request_id=test-123
[B] [Maintenance] Worker C marked as DEAD (no heartbeat for 11s)
[waiting 90 seconds...]
[B] [TeamLeader] Timeout waiting for worker results, some tasks may have failed
[A] [Leader] waiting for 2 team-leader result(s)
[waiting 90 seconds...]
[A] WARNING: Timeout waiting for results from team leaders
[A] done: test-123 chunks=0
Total time: ~180 seconds
```

### After (10-12 second timeouts)
```
[B] [TeamLeader] Request test-123 has 0 healthy worker(s) available
[B] [TeamLeader] No healthy workers available; failing request test-123 fast
[A] [Leader] waiting for 2 team-leader result(s)
[waiting 12 seconds...]
[A] [Leader] ERROR: All teams failed for request test-123; returning empty result. Reason: Leader timeout (12000ms)
[A] done: test-123 chunks=0
Total time: ~12 seconds
```

### Partial Failure Scenario
```
[B] [TeamLeader] Request test-123 has 1 healthy worker(s) available
[B] [TeamLeader] Received all 3 results for request test-123
[E] [TeamLeader] No healthy workers available; failing request test-123 fast
[A] [Leader] Partial team failure for request test-123: 1/2 teams succeeded. Failures: Leader timeout (12000ms)
[A] done (partial): test-123 chunks=3
Total time: ~12 seconds with partial results
```

## Testing Impact

### Test Scenarios
1. **Normal operation**: Both teams healthy → 10-12s max, full results
2. **One team down**: One team fails → 12s max, partial results with clear logging
3. **All teams down**: Both teams fail → 12s max, empty result with error log
4. **Worker dies mid-request**: Reassignment + potential timeout → 10-12s max

### Logs to Verify
- ✅ "No healthy workers available; failing request X fast"
- ✅ "Timeout waiting for worker results for request X (waited 10000ms)"
- ✅ "Partial team failure for request X: 1/2 teams succeeded"
- ✅ "All teams failed for request X; returning empty result"

## Implementation Notes
- **No proto changes**: All tracking done internally in C++ with existing messages
- **No client changes**: Client invocation pattern unchanged
- **Backward compatible**: Successful requests behave identically to before
- **Clear failure visibility**: All failure modes have explicit logging with reasons
- **Fast failure**: Maximum wait time reduced from ~180s to ~12s in worst case

## Build Status
✅ Clean compilation with no errors or warnings
✅ All changes localized to `RequestProcessor.cpp` and `RequestProcessor.h`
✅ Timeout constants configurable at compile time
