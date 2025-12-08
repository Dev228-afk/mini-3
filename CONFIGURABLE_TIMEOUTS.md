# Configurable Timeouts Implementation

## Overview
The Mini-3 distributed system now supports **configurable timeouts** via environment variables, allowing flexible timeout configuration for different experimental scenarios without code changes.

## Implementation Details

### 1. Environment Variable Helper Function
Added a generic helper function in `RequestProcessor.cpp`:

```cpp
inline std::chrono::milliseconds GetEnvMs(const char* name,
                                          std::chrono::milliseconds def_val) {
  const char* v = std::getenv(name);
  if (!v || *v == '\0') return def_val;
  try {
    long ms = std::stol(std::string(v));
    if (ms <= 0) return def_val;
    return std::chrono::milliseconds(ms);
  } catch (...) {
    return def_val;
  }
}
```

This function:
- Reads an environment variable by name
- Returns the default value if the variable is unset or invalid
- Validates that the value is a positive integer
- Handles exceptions gracefully

### 2. Configurable Constants

#### Leader Timeout (Node A)
**Environment Variable**: `MINI3_LEADER_TIMEOUT_MS`  
**Default Value**: 12000ms (12 seconds)  
**Purpose**: Timeout for global leader waiting for team results

```cpp
const std::chrono::milliseconds kLeaderWaitTimeoutMs =
    GetEnvMs("MINI3_LEADER_TIMEOUT_MS",
             std::chrono::milliseconds(12000));  // default 12s
```

**Where it's used**:
- `RequestProcessor::ProcessRequest()` - Leader waits for team leader responses
- Logged at startup when `SetTeamLeaders()` is called

#### Team Leader Timeout (Nodes B, E)
**Environment Variable**: `MINI3_TEAMLEADER_TIMEOUT_MS`  
**Default Value**: 10000ms (10 seconds)  
**Purpose**: Timeout for team leaders waiting for worker results

```cpp
const std::chrono::milliseconds kTeamLeaderWaitTimeoutMs =
    GetEnvMs("MINI3_TEAMLEADER_TIMEOUT_MS",
             std::chrono::milliseconds(10000));  // default 10s
```

**Where it's used**:
- `RequestProcessor::HandleTeamRequest()` - Team leaders wait for worker results
- Logged once on first request using `std::call_once`

### 3. Logging
Both timeout values are logged at startup for verification:

**Leader (Node A)**:
```
[A] [Leader] Using leader timeout = 12000 ms
```

**Team Leaders (Nodes B, E)**:
```
[B] [TeamLeader] Using worker wait timeout = 10000 ms
[E] [TeamLeader] Using worker wait timeout = 10000 ms
```

## Usage Scenarios

### Scenario 1: Default Behavior (Failure Experiments)
**Timeout Values**: Leader=12s, TeamLeader=10s  
**When to Use**: Fast-fail behavior for fault tolerance experiments

```bash
# No environment variables needed
./scripts/start_servers.sh
./scripts/run_client.sh --dataset test_data/data_1m.csv
```

**Characteristics**:
- Quick timeout detection (10-12 seconds)
- Suitable for crash/slow worker scenarios
- Demonstrates partial success handling
- Current behavior unchanged

### Scenario 2: Multi-Client Success Demo (Long Timeouts)
**Timeout Values**: Leader=30s, TeamLeader=25s  
**When to Use**: Ensuring all concurrent clients complete successfully

```bash
# Set longer timeouts
export MINI3_LEADER_TIMEOUT_MS=30000
export MINI3_TEAMLEADER_TIMEOUT_MS=25000

# Start servers with these timeouts
./scripts/start_servers.sh

# Run multi-client test
./scripts/run_multi_clients.sh --dataset test_data/data_1m.csv --clients 4
```

**Characteristics**:
- Allows more time for concurrent processing
- Reduces false timeout failures under load
- All 4 clients should receive their full 9 chunks
- Better for demonstrating system capacity

### Scenario 3: Custom Timeouts
**Timeout Values**: User-defined  
**When to Use**: Specific experimental requirements

```bash
# Set custom values
export MINI3_LEADER_TIMEOUT_MS=20000      # 20 seconds
export MINI3_TEAMLEADER_TIMEOUT_MS=15000  # 15 seconds

./scripts/start_servers.sh
```

## Configuration Guidelines

### Choosing Timeout Values

1. **Leader Timeout > Team Leader Timeout**
   - Always set `MINI3_LEADER_TIMEOUT_MS` > `MINI3_TEAMLEADER_TIMEOUT_MS`
   - Recommended gap: 2-5 seconds
   - Allows team leaders to report partial results before leader times out

2. **For Failure Experiments**
   - Short timeouts (default: 10-12s)
   - Fast failure detection
   - Demonstrates fault tolerance

3. **For Multi-Client Experiments**
   - Long timeouts (30s/25s recommended)
   - Accounts for concurrent load
   - Reduces spurious timeouts

4. **For Large Datasets**
   - Consider dataset size and worker capacity
   - 10M rows may need longer timeouts than 1M rows
   - Monitor actual completion times in logs

## Verification

### Check Configured Values in Logs
After starting servers, check the logs to verify timeout configuration:

```bash
# Check leader timeout (node A)
grep "Using leader timeout" /tmp/mini3_A/server.log

# Check team leader timeouts (nodes B, E)
grep "Using worker wait timeout" /tmp/mini3_B/server.log
grep "Using worker wait timeout" /tmp/mini3_E/server.log
```

Example output:
```
2025-12-08 ... [A] [Leader] Using leader timeout = 30000 ms
2025-12-08 ... [B] [TeamLeader] Using worker wait timeout = 25000 ms
2025-12-08 ... [E] [TeamLeader] Using worker wait timeout = 25000 ms
```

### Test Different Configurations
Use the provided test script:

```bash
cd scripts
./test_timeout_configs.sh
```

## Technical Notes

### Why Environment Variables?
- **No code changes** required for different scenarios
- **Easy deployment** - set once before starting servers
- **Documented defaults** - behavior unchanged without env vars
- **Logging** - values visible in server logs for verification

### Thread Safety
- `GetEnvMs()` called during static initialization (before main)
- Timeout constants initialized once at program startup
- Thread-safe by design (initialization happens before threading)
- `std::call_once` ensures single log per process

### Backward Compatibility
- **100% backward compatible** - no changes if env vars unset
- Default values match previous hardcoded constants
- Existing scripts and workflows continue working
- Only adds new capability, doesn't change existing behavior

## Examples

### Running Experiments with Different Timeouts

#### Short Timeout (Crash Experiment)
```bash
# Use defaults or explicitly set short timeouts
export MINI3_LEADER_TIMEOUT_MS=12000
export MINI3_TEAMLEADER_TIMEOUT_MS=10000

./scripts/start_servers.sh
sleep 5

# Crash worker C after 2 seconds
(sleep 2 && pkill -f "mini2_server.*node_id=C") &

./scripts/run_client.sh --dataset test_data/data_1m.csv
```

Expected: System detects C's failure, returns partial results (6 chunks) within 12 seconds

#### Long Timeout (Multi-Client Success)
```bash
# Set long timeouts for multi-client scenario
export MINI3_LEADER_TIMEOUT_MS=30000
export MINI3_TEAMLEADER_TIMEOUT_MS=25000

./scripts/start_servers.sh
sleep 5

# Run 4 concurrent clients
./scripts/run_multi_clients.sh --dataset test_data/data_1m.csv --clients 4
```

Expected: All 4 clients receive 9 chunks each, no timeouts

## Summary of Changes

### Modified Files
- `src/cpp/server/RequestProcessor.cpp`
  - Added `GetEnvMs()` helper function
  - Changed `kLeaderWaitTimeoutMs` from `constexpr` to `const` with env var support
  - Changed `kTeamLeaderWaitTimeoutMs` from `constexpr` to `const` with env var support
  - Added timeout logging in `SetTeamLeaders()` (for leader A)
  - Added timeout logging in `HandleTeamRequest()` (for team leaders B/E)

### New Files
- `scripts/test_timeout_configs.sh` - Test script for timeout configuration
- `CONFIGURABLE_TIMEOUTS.md` - This documentation

### No Changes Required
- Client code
- Test scripts (backward compatible)
- Network configuration
- Other server components

## Future Enhancements

Potential future improvements:
1. **Worker timeout**: Make worker dead detection timeout configurable
2. **Health check interval**: Configurable heartbeat frequency
3. **Configuration file**: Support JSON/YAML config in addition to env vars
4. **Runtime updates**: Allow timeout changes without restart (more complex)
5. **Per-team timeouts**: Different timeouts for green vs pink teams

## Troubleshooting

### Issue: Timeouts still too short
**Solution**: Increase env var values and restart servers

### Issue: Values not taking effect
**Check**:
1. Env vars set before starting servers: `echo $MINI3_LEADER_TIMEOUT_MS`
2. Server logs show expected values: `grep "timeout" /tmp/mini3_*/server.log`
3. Servers restarted after setting env vars

### Issue: Invalid value warnings
**Cause**: Non-numeric or negative values in env vars  
**Solution**: Ensure env vars contain positive integers (milliseconds)

```bash
# Correct
export MINI3_LEADER_TIMEOUT_MS=30000

# Incorrect (will use default)
export MINI3_LEADER_TIMEOUT_MS=30s      # No units allowed
export MINI3_LEADER_TIMEOUT_MS=-1000    # Negative not allowed
export MINI3_LEADER_TIMEOUT_MS=abc      # Not a number
```
