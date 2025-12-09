# Mini-3 Experiment Guide

This document explains how to run the Mini-3 experiments for fault-tolerant distributed data processing.

## Prerequisites

1. Build the project:
   ```bash
   ./scripts/build.sh
   ```

2. Generate test datasets (if needed):
   ```bash
   python3 test_data/gen_test_data.py
   ```

3. Ensure all 6 nodes can communicate:
   - Edit `config/network_setup.json` with correct IPs/hostnames
   - On Windows: Run `scripts/setup_windows_firewall.ps1` to open ports 50051-50056

## System Architecture

**6-Node Topology:**
- **Leader A**: Main coordinator (receives client requests)
- **Team Leaders B, E**: Forward requests to workers
- **Workers C, D, F**: Process CSV chunks

**Distribution:**
- Computer 1: A, B, D
- Computer 2: C, E, F

## Experiment 1: Single Client Baseline

Test system with one client, no faults.

```bash
# Start servers (Computer 1)
./scripts/start_servers.sh --computer 1

# Start servers (Computer 2) - if using 2 PCs
./scripts/start_servers.sh --computer 2

# Run client
./scripts/test_real_data.sh --dataset test_data/data_1m.csv
```

**Output:** Client prints chunk latencies, TTFB, total time, throughput

## Experiment 2: Multi-Client Concurrent

Test system with N concurrent clients hitting same dataset.

```bash
# Run 4 concurrent clients (restarts servers automatically)
./scripts/run_multi_clients.sh --dataset test_data/data_1m.csv --clients 4 --label mc4_1m --computer 1

# Note: If using 2 PCs, start PC-2 servers first:
#   (PC-2) ./scripts/start_servers.sh --computer 2
```

**Output:** Each client logs independently, script aggregates metrics

## Experiment 3: Worker Crash Scenarios

Simulate worker crashes to test fault tolerance.

```bash
# Start servers normally
./scripts/start_servers.sh --computer 1
./scripts/start_servers.sh --computer 2

# Kill worker C mid-processing
pkill -f "mini2_server --.*--node C"

# Run client - should see partial success (6/9 chunks)
./scripts/test_real_data.sh --dataset test_data/data_10m.csv
```

**Expected:** System delivers partial results, timeouts after 10-12s

## Experiment 4: Worker Slowdown

Simulate slow worker (e.g., weak hardware).

```bash
# Configure worker D to be slow (5s delay per task)
export MINI3_SLOW_D_MS=5000

# Start servers
./scripts/start_servers.sh --computer 1
./scripts/start_servers.sh --computer 2

# Run client - should see reduced throughput
./scripts/test_real_data.sh --dataset test_data/data_5m.csv
```

**Expected:** Capacity-aware scheduler avoids slow worker D

## Configurable Timeouts

Two environment variables control failure detection speed:

```bash
# Fast-fail mode (for fault tolerance experiments)
export MINI3_LEADER_TIMEOUT_MS=12000      # Leader A waits 12s for teams
export MINI3_TEAMLEADER_TIMEOUT_MS=10000  # Team leaders wait 10s for workers

# Success mode (for multi-client experiments, avoid false timeouts)
export MINI3_LEADER_TIMEOUT_MS=30000      # 30s
export MINI3_TEAMLEADER_TIMEOUT_MS=25000  # 25s
```

**Default values:** 12s / 10s (if not set)

## Log Collection & Analysis

### Capture logs from an experiment:

```bash
# Mark log position before running client
MARK_FILE=$(./scripts/mark_logs.sh "experiment_label")

# Run experiment
./scripts/test_real_data.sh --dataset test_data/data_1m.csv

# Extract logs since mark
./scripts/slice_since_mark.sh "$MARK_FILE" "experiment_label"

# Output: logs/logs_<label>_short.txt (filtered & combined)
```

### Parse logs to CSV:

```bash
# Extract metrics from all logs
python3 tools/extract_metrics.py --glob 'logs/logs_*.txt' --output results/metrics.csv

# Fields: dataset, rows, scenario, chunks, bytes, timing, worker_tasks, throughput
```

### Generate charts:

```bash
python3 tools/plot_throughput.py
# Output: results/throughput_chart.png
```

## Two-PC Orchestration (Advanced)

For experiments spanning 2 physical computers:

```bash
# PC-1 (has dataset files)
./scripts/run_experiment_pc1.sh test_data/data_1m.csv exp_1m

# PC-2 (remote, no dataset needed)
./scripts/run_experiment_pc2.sh exp_1m

# Or coordinate both via SSH:
./scripts/run_experiment_pair.sh \
  --dataset test_data/data_1m.csv \
  --label exp_1m \
  --remote-host user@pc2-hostname \
  --remote-root /path/to/mini_3
```

## Stopping Servers

```bash
# Kill all mini2_server processes
pkill -f mini2_server

# Or individual nodes
pkill -f "mini2_server.*--node A"
```

## Troubleshooting

**Problem:** Client timeout (DEADLINE_EXCEEDED)
- **Solution:** Increase timeouts via env vars (see above)

**Problem:** Workers not receiving tasks
- **Solution:** Check `logs/server_<NODE>.log` for heartbeat messages

**Problem:** "Connection refused"
- **Solution:** Verify firewall rules, check IP addresses in `config/network_setup.json`

**Problem:** Inconsistent results
- **Solution:** Clear logs before each run: `./scripts/clear_logs.sh`

## Summary of Key Scripts

| Script | Purpose |
|--------|---------|
| `build.sh` | Compile C++ code |
| `start_servers.sh` | Launch servers by computer/node |
| `test_real_data.sh` | Run single client (Strategy B) |
| `run_multi_clients.sh` | Run N concurrent clients |
| `mark_logs.sh` | Mark current log position |
| `slice_since_mark.sh` | Extract logs since mark |
| `clear_logs.sh` | Clean logs/ directory |
| `extract_metrics.py` | Parse logs to CSV |
| `plot_throughput.py` | Generate charts |

## Example Workflow

Complete workflow for a new experiment:

```bash
# 1. Build
./scripts/build.sh

# 2. Clear old logs
./scripts/clear_logs.sh

# 3. Start servers
./scripts/start_servers.sh --computer 1

# 4. Mark log position
MARK=$(./scripts/mark_logs.sh "test_10m")

# 5. Run experiment
./scripts/test_real_data.sh --dataset test_data/data_10m.csv

# 6. Extract logs
./scripts/slice_since_mark.sh "$MARK" "test_10m"

# 7. Parse to CSV
python3 tools/extract_metrics.py --glob 'logs/logs_test_10m_short.txt' --output results/test_10m.csv

# 8. Stop servers
pkill -f mini2_server
```

## Notes

- **Strategy B (GetNextChunk):** Client pulls chunks sequentially (used in all experiments)
- **Session IDs:** Unique per-client to prevent multi-client collisions
- **Chunk count:** Typically 9 chunks (3 per team leader × 3 workers), varies by dataset size
- **Partial success:** System returns available chunks when some workers fail/timeout
