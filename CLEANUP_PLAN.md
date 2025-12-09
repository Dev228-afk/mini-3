# Mini-3 Repository Map & Cleanup Plan

## Repository Structure

```
mini_3/
├── src/cpp/                    # Main C++ source code
│   ├── client/ClientMain.cpp   # Client binary (mini2_client)
│   ├── server/                 # Server components
│   │   ├── ServerMain.cpp      # Server entry point (mini2_server)
│   │   ├── Handlers.cpp        # gRPC service implementations
│   │   ├── RequestProcessor.*  # Core request processing logic
│   │   ├── SessionManager.*    # Session tracking & correlation
│   │   ├── DataProcessor.*     # CSV parsing & chunk processing
│   │   └── WorkerQueue.*       # Unused? (not found in Handlers/ServerMain)
│   ├── common/                 # Shared utilities
│   │   ├── config.*            # JSON config parsing
│   │   ├── logging.h           # Log macros
│   │   └── minitwo.pb/grpc.*   # Generated protobuf code
│   └── tools/
│       └── inspect_shm.cpp     # Legacy Mini-2 shared memory tool
│
├── scripts/                    # Experiment automation
│   ├── build.sh                # CMake build wrapper
│   ├── start_servers.sh        # Launch servers by computer/node
│   ├── start_node.sh           # Launch single server
│   ├── test_real_data.sh       # Run single client (Strategy B)
│   ├── run_multi_clients.sh    # Run N concurrent clients
│   ├── run_experiment_pc1.sh   # PC-1 experiment orchestration
│   ├── run_experiment_pc2.sh   # PC-2 experiment orchestration
│   ├── run_experiment_pair.sh  # Coordinate PC-1 & PC-2 via SSH
│   ├── capture_run.sh          # Wrapper to capture logs
│   ├── mark_logs.sh            # Mark log position before run
│   ├── slice_since_mark.sh     # Extract logs since mark
│   ├── make_short_log.sh       # Combine & filter logs
│   ├── capture_local_logs.sh   # Legacy?
│   ├── clear_logs.sh           # Clean logs/ directory
│   ├── diagnose_network.sh     # Network debugging tool
│   ├── gen_proto.sh            # Regenerate protobuf code
│   ├── run_client.sh           # Duplicate of test_real_data.sh?
│   ├── test_timeout_configs.sh # Timeout testing
│   ├── slice_logs_by_time.sh   # Time-based log slicing
│   └── setup_windows_firewall.ps1  # Windows firewall config
│
├── tools/                      # Analysis scripts
│   ├── extract_metrics.py      # Parse logs to CSV (USED)
│   └── plot_throughput.py      # Generate charts (USED)
│
├── config/
│   ├── network_setup.json      # Active 6-node topology
│   └── network_setup_*.json    # Legacy experiments
│
├── results/                    # Experiment outputs
│   ├── mini3_metrics.csv       # Parsed from logs/
│   ├── logggg_metrics.csv      # Parsed from logggg/
│   └── throughput_chart.png    # Generated visualization
│
├── logs/                       # Mini-3 fault tolerance logs (13 files)
├── logggg/                     # Mini-3 multi-client logs (10 files)
├── test_data/                  # CSV datasets (generated)
└── tests/                      # Unit tests
```

---

## Core Paths (DO NOT BREAK)

### Binaries & Entry Points
- ✅ **mini2_client** (`src/cpp/client/ClientMain.cpp`)
  - Used by: `test_real_data.sh`, `run_multi_clients.sh`
  - CLI: `--config`, `--gateway`, `--dataset`
  - Strategy B: GetNext (sequential chunk pull)
  
- ✅ **mini2_server** (`src/cpp/server/ServerMain.cpp`)
  - Used by: All `start_*.sh` scripts
  - CLI: `--config`, `--node`
  - Roles: A (leader), B/E (team leaders), C/D/F (workers)

### Core Server Components
- ✅ **Handlers.cpp** - gRPC service implementations
  - `ClientGateway` (StartRequest, GetNextChunk)
  - `TeamIngress` (HandleRequest, PushWorkerResult)
  - `WorkerControl` (RequestTask, ReportHealth)
  - `NodeControl` (Ping, OpenSession)

- ✅ **RequestProcessor.{cpp,h}** - Request routing & coordination
  - Leader A → Team Leaders B/E
  - Team Leaders → Workers C/D/F
  - Timeout handling (configurable via env vars)
  - Partial success delivery

- ✅ **SessionManager.{cpp,h}** - Session tracking
  - Unique session_id generation
  - Multi-client request correlation
  - Chunk storage & retrieval

- ✅ **DataProcessor.{cpp,h}** - CSV parsing & chunking
  - File I/O
  - CSV parsing
  - Chunk splitting (3-9 chunks)

### Key Scripts (Used in Experiments)
- ✅ **test_real_data.sh** - Single client baseline
- ✅ **run_multi_clients.sh** - Multi-client concurrency tests
- ✅ **start_servers.sh** - Server launch automation
- ✅ **mark_logs.sh**, **slice_since_mark.sh**, **make_short_log.sh** - Log processing
- ✅ **run_experiment_pc1.sh**, **run_experiment_pc2.sh** - Two-PC orchestration

### Analysis Tools
- ✅ **tools/extract_metrics.py** - Parse logs to CSV (USED for results/)
- ✅ **tools/plot_throughput.py** - Generate charts (USED for report)

### Protobuf
- ✅ **protos/minitwo.proto** - Service & message definitions
  - DO NOT change field names/numbers
  - DO NOT change service names

---

## Probably Keep But Could Simplify

### Scripts with Unclear Usage
- ⚠️ **run_client.sh** - Appears to duplicate `test_real_data.sh`
  - Compare both files, merge if identical logic
  
- ⚠️ **capture_run.sh** - Wrapper around test_real_data.sh
  - Check if used by any experiments
  - If unused, mark as legacy
  
- ⚠️ **capture_local_logs.sh** - Log collection
  - Overlaps with `make_short_log.sh`?
  - Check actual usage

- ⚠️ **test_timeout_configs.sh** - Timeout testing
  - May be one-off experiment script
  - Keep but document as "testing only"

- ⚠️ **slice_logs_by_time.sh** - Time-based log slicing
  - Less used than `slice_since_mark.sh`
  - Keep as alternative method

### Config Files
- ⚠️ **config/network_setup_*.json** (ring, windows, with_python)
  - Legacy from Mini-2 experiments
  - Keep for reference but document as "unused"

### Documentation
- ⚠️ **HEALTH_HANDLING_SUMMARY.md**, **LOGGING_CHANGES.md**, etc.
  - Verbose AI-generated docs
  - Simplify to 1-2 page summaries

---

## Safe Candidates for Deletion

### Completely Unused Code

1. **src/cpp/tools/inspect_shm.cpp**
   - Purpose: Inspect shared memory segments (Mini-2 Phase 4)
   - Mini-3 doesn't use shared memory
   - Not referenced by any script
   - **ACTION: DELETE** (+ remove from CMakeLists.txt)

2. **src/cpp/server/WorkerQueue.{cpp,h}**
   - Purpose: Multi-threaded request queue
   - NOT used by ServerMain.cpp or Handlers.cpp
   - Appears to be experimental/unused infrastructure
   - **ACTION: Check if truly unused, then DELETE**

3. **tests/cpp_unit_tests.cpp**
   - Purpose: Sanity tests
   - Not run by any experiment script
   - **ACTION: Keep if compiles, else DELETE**

4. **tests/python_unit_tests.py**
   - Purpose: Python component tests
   - No Python server in Mini-3
   - **ACTION: DELETE if unused**

### Legacy Scripts

5. **scripts/setup_windows_firewall.ps1**
   - One-time setup script
   - Not part of experiment workflow
   - **ACTION: Move to docs/ or DELETE**

6. **scripts/diagnose_network.sh**
   - Debugging tool
   - Not part of core experiments
   - **ACTION: Mark as "troubleshooting tool" or DELETE**

### Python Server (if exists)
7. **src/python/server/** (if present)
   - Mini-3 experiments use C++ servers only
   - **ACTION: DELETE if not referenced**

### Old Experiment Results
8. **localhost_results/** directory
   - Appears to be Mini-2 results
   - **ACTION: Archive or DELETE (keep logs/ and logggg/ only)**

---

## Naming Cleanup Strategy

### C++ Naming Conventions

**Current Issues:**
- Mix of CamelCase and snake_case
- Overly long function names
- Generic "Handler" names

**Proposed Changes:**

| Current | Proposed | Rationale |
|---------|----------|-----------|
| `ClientGatewayServiceImpl` | `ClientGateway` | Already used in code |
| `TeamIngressServiceImpl` | `TeamIngress` | Match service name |
| `WorkerControlServiceImpl` | `WorkerControl` | Match service name |
| `ProcessInboundRequest` | `handle_request` | Simpler, student-like |
| `GenerateUniqueSessionId` | `generate_session_id` | Shorter |
| `HandleTeamRequest` | `handle_team_request` | Consistent snake_case |

**Rules:**
- Keep protobuf message/field names unchanged
- Keep gRPC service method names unchanged (e.g., `StartRequest`, `GetNextChunk`)
- Internal functions: `lower_snake_case`
- Classes: `CamelCase`
- Variables: `lower_snake_case`

### Script Naming
- All scripts already use `snake_case.sh` ✅
- No changes needed

---

## Simplification Plan

### Phase 1: Delete Unused Code
1. Remove `inspect_shm.cpp` and CMake target
2. Verify `WorkerQueue.*` is unused, then remove
3. Delete `python_unit_tests.py` if unused
4. Delete/archive `localhost_results/`
5. Remove legacy config files or move to `config/legacy/`

### Phase 2: Consolidate Scripts
1. Compare `run_client.sh` vs `test_real_data.sh` → merge or delete
2. Decide on `capture_run.sh` fate
3. Move troubleshooting scripts to `scripts/debug/` subdirectory

### Phase 3: Simplify Docs
1. Merge verbose .md files into concise README sections
2. Create single `EXPERIMENTS.md` with:
   - How to run single client
   - How to run multi-client
   - How to extract metrics
   - How to generate charts

### Phase 4: Code Cleanup
1. Shorten AI-verbose comments
2. Apply consistent naming within each .cpp file
3. Add brief file headers (2-3 lines: purpose, used by)

---

## Validation Checklist

After cleanup, verify these still work:

- [ ] `./scripts/build.sh` compiles successfully
- [ ] `./scripts/test_real_data.sh --dataset test_data/data_10k.csv` runs
- [ ] `./scripts/run_multi_clients.sh --dataset ... --clients 4` runs
- [ ] `python3 tools/extract_metrics.py --glob 'logs/*.txt'` parses logs
- [ ] `python3 tools/plot_throughput.py` generates chart
- [ ] No broken imports/includes after deletions
- [ ] CMakeLists.txt builds all needed targets

---

## Output Format

### Changelog Structure
```
## Deleted Files
- src/cpp/tools/inspect_shm.cpp (Mini-2 shared memory tool)
- src/cpp/server/WorkerQueue.* (unused queue infrastructure)
- tests/python_unit_tests.py (no Python server in Mini-3)
...

## Renamed Functions
- ProcessInboundRequest → handle_request (RequestProcessor.cpp)
- GenerateUniqueSessionId → generate_session_id (SessionManager.cpp)
...

## Simplified
- Combined 5 verbose .md files into EXPERIMENTS.md
- Removed AI-ish comments from DataProcessor.cpp
- Consolidated duplicate log parsing scripts
...
```

### How to Run (Final)
```bash
# Single-client baseline
./scripts/test_real_data.sh --dataset test_data/data_1m.csv

# Multi-client same dataset (4 concurrent)
./scripts/run_multi_clients.sh --dataset test_data/data_1m.csv --clients 4 --label mc4_1m

# Extract metrics from logs
python3 tools/extract_metrics.py --glob 'logs/*.txt' --output results/metrics.csv

# Generate throughput chart
python3 tools/plot_throughput.py
```

---

## Next Steps

1. Review this plan
2. Confirm deletions are safe
3. Execute cleanup in phases
4. Test after each phase
5. Generate final changelog
