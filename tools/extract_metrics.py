# #!/usr/bin/env python3
# """
# Extract metrics from combined logs_testdata_*.txt files and produce metrics CSV.
# """

# import re
# import glob
# import csv
# import os
# from datetime import datetime
# from pathlib import Path


# def parse_timestamp(line: str) -> datetime:
#     """Extract timestamp from log line prefix: YYYY-MM-DD HH:MM:SS.mmm"""
#     match = re.match(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})', line)
#     if match:
#         return datetime.strptime(match.group(1), '%Y-%m-%d %H:%M:%S.%f')
#     return None


# def extract_float_or_ms(text: str) -> int:
#     """Convert '11.623 s' to 11623 or '15346 ms' to 15346"""
#     match = re.search(r'([\d.]+)\s*(s|ms)', text)
#     if match:
#         value = float(match.group(1))
#         unit = match.group(2)
#         if unit == 's':
#             return int(value * 1000)
#         else:  # ms
#             return int(value)
#     return None


# def extract_dataset_info(content: str, filename: str):
#     """Extract dataset name and row count from content or filename"""
#     # Try to extract from PROCESSING DATASET line
#     dataset_match = re.search(r'PROCESSING DATASET:\s*test_data/(data_\w+\.csv)', content)
#     if dataset_match:
#         dataset = dataset_match.group(1)
#     else:
#         # Fall back to filename
#         fname_match = re.search(r'logs_testdata_(\w+)\.txt', filename)
#         if fname_match:
#             dataset = f"data_{fname_match.group(1)}.csv"
#         else:
#             dataset = "unknown.csv"
    
#     # Convert dataset name to row count
#     rows_map = {
#         '1k': 1000,
#         '5k': 5000,
#         '10k': 10000,
#         '50k': 50000,
#         '100k': 100000,
#         '200k': 200000,
#         '500k': 500000,
#         '1m': 1000000,
#         '5m': 5000000,
#         '10m': 10000000
#     }
    
#     for key, value in rows_map.items():
#         if key in dataset.lower():
#             return dataset, value
    
#     return dataset, 0


# def parse_team_metrics(content: str, team_leader: str, workers: list) -> dict:
#     """Parse metrics for a single team (B+C or E+D/F)"""
#     metrics = {
#         'leader_ms': None,
#         'worker_window_ms': None,
#         'worker_tasks': {w: 0 for w in workers}
#     }
    
#     # Find team leader start and end
#     leader_start = None
#     leader_end = None
    
#     for line in content.split('\n'):
#         if f'[TeamLeader {team_leader}]' in line and 'HandleTeamRequest' in line and 'test-strategyB-getnext' in line:
#             ts = parse_timestamp(line)
#             if ts and leader_start is None:
#                 leader_start = ts
        
#         if f'[TeamLeader {team_leader}]' in line and 'Received all 3 results' in line and 'test-strategyB-getnext' in line:
#             ts = parse_timestamp(line)
#             if ts:
#                 leader_end = ts
    
#     if leader_start and leader_end:
#         metrics['leader_ms'] = int((leader_end - leader_start).total_seconds() * 1000)
    
#     # Find worker window (first pull to last finish)
#     worker_pulls = []
#     worker_finishes = []
    
#     for line in content.split('\n'):
#         for worker in workers:
#             if f'[{worker}]' in line and '[WorkerLoop] Pulled task test-strategyB-getnext' in line:
#                 ts = parse_timestamp(line)
#                 if ts:
#                     worker_pulls.append(ts)
            
#             if f'[{worker}]' in line and '[WorkerLoop] Finished task test-strategyB-getnext' in line:
#                 ts = parse_timestamp(line)
#                 if ts:
#                     worker_finishes.append(ts)
#                 # Count tasks per worker
#                 metrics['worker_tasks'][worker] += 1
    
#     if worker_pulls and worker_finishes:
#         first_pull = min(worker_pulls)
#         last_finish = max(worker_finishes)
#         metrics['worker_window_ms'] = int((last_finish - first_pull).total_seconds() * 1000)
    
#     return metrics


# def parse_client_metrics(content: str) -> dict:
#     """Parse client summary metrics (TTFB, total time, chunks)"""
#     metrics = {
#         'ttfb_ms': None,
#         'total_ms': None,
#         'total_chunks': None
#     }
    
#     # Find Strategy B section
#     strategy_b_section = re.search(
#         r'Strategy B \(GetNext\) Results:.*?(?=\n\n|\Z)',
#         content,
#         re.DOTALL
#     )
    
#     if strategy_b_section:
#         section_text = strategy_b_section.group(0)
        
#         # Extract TTFB
#         ttfb_match = re.search(r'Time to first chunk:\s*([\d.]+\s*(?:s|ms))', section_text)
#         if ttfb_match:
#             metrics['ttfb_ms'] = extract_float_or_ms(ttfb_match.group(1))
        
#         # Extract total time
#         total_match = re.search(r'Total time:\s*([\d.]+\s*(?:s|ms))', section_text)
#         if total_match:
#             metrics['total_ms'] = extract_float_or_ms(total_match.group(1))
        
#         # Extract total chunks
#         chunks_match = re.search(r'Total chunks:\s*(\d+)', section_text)
#         if chunks_match:
#             metrics['total_chunks'] = int(chunks_match.group(1))
    
#     return metrics


# def process_log_file(filepath: str) -> list:
#     """Process a single log file and return list of metric rows"""
#     rows = []
    
#     with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
#         content = f.read()
    
#     # Extract dataset info
#     dataset, row_count = extract_dataset_info(content, os.path.basename(filepath))
    
#     # Parse client metrics (shared by both teams)
#     client_metrics = parse_client_metrics(content)
    
#     # Parse green team (B + C)
#     green_metrics = parse_team_metrics(content, 'B', ['C'])
#     rows.append({
#         'dataset': dataset,
#         'rows': row_count,
#         'team': 'green',
#         'leader_ms': green_metrics['leader_ms'],
#         'worker_window_ms': green_metrics['worker_window_ms'],
#         'ttfb_ms': client_metrics['ttfb_ms'],
#         'total_ms': client_metrics['total_ms'],
#         'total_chunks': client_metrics['total_chunks'],
#         'worker_C_tasks': green_metrics['worker_tasks']['C'],
#         'worker_D_tasks': 0,
#         'worker_F_tasks': 0
#     })
    
#     # Parse pink team (E + D + F)
#     pink_metrics = parse_team_metrics(content, 'E', ['D', 'F'])
#     rows.append({
#         'dataset': dataset,
#         'rows': row_count,
#         'team': 'pink',
#         'leader_ms': pink_metrics['leader_ms'],
#         'worker_window_ms': pink_metrics['worker_window_ms'],
#         'ttfb_ms': client_metrics['ttfb_ms'],
#         'total_ms': client_metrics['total_ms'],
#         'total_chunks': client_metrics['total_chunks'],
#         'worker_C_tasks': 0,
#         'worker_D_tasks': pink_metrics['worker_tasks']['D'],
#         'worker_F_tasks': pink_metrics['worker_tasks']['F']
#     })
    
#     return rows


# def main():
#     """Main entry point"""
#     # Find all log files
#     log_files = []
    
#     # Check current directory
#     log_files.extend(glob.glob('logs_testdata_*.txt'))
    
#     # Check logs/ directory if it exists
#     if os.path.isdir('logs'):
#         log_files.extend(glob.glob('logs/logs_testdata_*.txt'))
    
#     if not log_files:
#         print("No log files found matching pattern logs_testdata_*.txt")
#         return
    
#     print(f"Found {len(log_files)} log file(s)")
    
#     # Process all files
#     all_rows = []
#     for filepath in sorted(log_files):
#         print(f"Processing {filepath}...")
#         rows = process_log_file(filepath)
#         all_rows.extend(rows)
    
#     # Ensure results directory exists
#     os.makedirs('results', exist_ok=True)
    
#     # Write CSV
#     output_path = 'results/metrics_strategyB.csv'
#     fieldnames = [
#         'dataset',
#         'rows',
#         'team',
#         'leader_ms',
#         'worker_window_ms',
#         'ttfb_ms',
#         'total_ms',
#         'total_chunks',
#         'worker_C_tasks',
#         'worker_D_tasks',
#         'worker_F_tasks'
#     ]
    
#     with open(output_path, 'w', newline='') as csvfile:
#         writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
#         writer.writeheader()
#         writer.writerows(all_rows)
    
#     print(f"\nProcessed {len(log_files)} file(s), wrote {len(all_rows)} row(s) to {output_path}")


# if __name__ == '__main__':
#     main()


#!/usr/bin/env python3
"""
extract_metrics.py

Parse combined Mini-3 server logs from 6 nodes (A, B, C, D, E, F):
    logs_1m_normal.txt - Normal operation
    logs_1m_crashC.txt - Worker C crashed
    logs_1m_slowCD.txt - Workers C and D slowed down

These logs contain only server-side logs (no client output):
  - Server A: Gateway, SessionManager, Leader coordination
  - Server B: Team leader (green team with worker C)
  - Server E: Team leader (pink team with workers D, F)
  - Workers C, D, F: Task execution logs

This script extracts:
  - Dataset and row count (from server logs or filename)
  - Scenario (normal/crash/slow from filename)
  - Total chunks delivered (from SessionManager or Leader)
  - Worker task counts (C, D, F)
  - Timing metrics from server logs

Usage:
    cd /path/to/mini-3
    python3 tools/extract_metrics.py --glob 'logs/logs_*.txt'

Outputs:
    results/mini3_metrics.csv
"""

import argparse
import csv
import os
import re
from pathlib import Path
from typing import Dict, Any, Optional, List, Tuple
from datetime import datetime


# ---------- Helpers ----------

def parse_timestamp(line: str) -> Optional[datetime]:
    """Extract timestamp from log line: YYYY-MM-DD HH:MM:SS.mmm"""
    m = re.match(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})', line)
    if m:
        try:
            return datetime.strptime(m.group(1), '%Y-%m-%d %H:%M:%S.%f')
        except ValueError:
            return None
    return None


def parse_time_value(s: str) -> Optional[float]:
    """
    Parse time strings like '11.623 s' or '15346 ms' into milliseconds (float).
    Returns None if no match.
    """
    s = s.strip()
    m = re.search(r'([0-9]+(?:\.[0-9]+)?)\s*(ms|s)\b', s)
    if not m:
        return None
    val = float(m.group(1))
    unit = m.group(2)
    if unit == 's':
        return val * 1000.0
    return val  # ms


def infer_rows_from_dataset(dataset: str) -> Optional[int]:
    """
    Infer row count from dataset path/filename, e.g.:
      test_data/data_1k.csv -> 1000
      test_data/data_5k.csv -> 5000
      test_data/data_10m.csv -> 10_000_000
    Returns None if it can't infer.
    """
    name = os.path.basename(dataset)
    # Look for patterns like '1k', '5k', '10k', '1m', '5m', '10m'
    m = re.search(r'(\d+)\s*([kKmM])', name)
    if not m:
        return None
    num = int(m.group(1))
    unit = m.group(2).lower()
    if unit == 'k':
        return num * 1_000
    elif unit == 'm':
        return num * 1_000_000
    return None


def infer_scenario_from_filename(fname: str) -> str:
    """
    Try to infer scenario from filename:
      logs_1m_normal.txt -> normal
      logs_1m_slowCD.txt -> slow
      logs_1m_crashC.txt -> crash
    """
    lower = fname.lower()
    if "slow" in lower:
        return "slow"
    if "crash" in lower:
        return "crash"
    if "normal" in lower:
        return "normal"
    return "unknown"


def find_first(regex: re.Pattern, lines: List[str]) -> Optional[re.Match]:
    for line in lines:
        m = regex.search(line)
        if m:
            return m
    return None


def find_all(regex: re.Pattern, lines: List[str]) -> List[re.Match]:
    return [regex.search(line) for line in lines if regex.search(line)]


# ---------- Core parsing per file ----------

def parse_log_file(path: Path) -> Dict[str, Any]:
    """Parse a combined server log file (6 nodes: A, B, C, D, E, F)"""
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    result: Dict[str, Any] = {
        "file": path.name,
        "dataset": "",
        "rows": None,
        "scenario": infer_scenario_from_filename(path.name),
        "status": "unknown",
        "ttfb_ms": None,  # Not available in server-only logs
        "total_ms": None,  # Calculated from server timestamps
        "total_chunks": None,
        "total_bytes": None,  # Sum from worker logs
        "throughput_MBps": None,  # Calculated if we have bytes and time
        "worker_C_tasks": 0,
        "worker_D_tasks": 0,
        "worker_F_tasks": 0,
    }

    # --- Extract dataset from server logs ---
    # Look for: "[B] Loading dataset from query: test_data/data_1m.csv"
    # or: "HandleTeamRequest: processing request_id=... dataset=test_data/data_1m.csv"
    m = find_first(re.compile(r'dataset=(\S+\.csv)', re.IGNORECASE), lines)
    if not m:
        m = find_first(re.compile(r'Loading dataset from query:\s*(\S+\.csv)', re.IGNORECASE), lines)
    if not m:
        m = find_first(re.compile(r'\[DataProcessor\]\s+loading\s+(\S+\.csv)', re.IGNORECASE), lines)
    
    if m:
        dataset = m.group(1)
        result["dataset"] = dataset
        rows = infer_rows_from_dataset(dataset)
        result["rows"] = rows

    # Fallback: infer from filename if no dataset found
    if not result["dataset"]:
        fname = path.name
        m2 = re.search(r'(\d+)\s*([kKmM])', fname)
        if m2:
            unit_num = int(m2.group(1))
            unit = m2.group(2).lower()
            if unit == 'k':
                result["rows"] = unit_num * 1000
            elif unit == 'm':
                result["rows"] = unit_num * 1_000_000
            result["dataset"] = f"inferred_from_filename_{unit_num}{unit}"

    # --- Extract total chunks from Server A logs ---
    # Look for: "[Leader] done (partial): test-strategyB-getnext chunks=9"
    # or: "[SessionManager] done session ... chunks=9"
    m = find_first(re.compile(r'\[Leader\].*done.*chunks=(\d+)', re.IGNORECASE), lines)
    if not m:
        m = find_first(re.compile(r'\[SessionManager\].*done session.*chunks=(\d+)', re.IGNORECASE), lines)
    if m:
        result["total_chunks"] = int(m.group(1))

    # --- Count worker tasks and sum bytes ---
    # Pattern: "2025-12-07 18:24:26.294 DEBUG [D] [WorkerLoop] Finished task test-strategyB-getnext.5 in 738.441000ms"
    # Pattern: "2025-12-07 18:24:26.402 DEBUG [F] [Worker] Generated 20642423 bytes for task test-strategyB-getnext.1"
    
    pattern_C_finish = re.compile(r'\[C\].*\[WorkerLoop\].*Finished task test-strategyB-getnext', re.IGNORECASE)
    pattern_D_finish = re.compile(r'\[D\].*\[WorkerLoop\].*Finished task test-strategyB-getnext', re.IGNORECASE)
    pattern_F_finish = re.compile(r'\[F\].*\[WorkerLoop\].*Finished task test-strategyB-getnext', re.IGNORECASE)
    
    pattern_bytes = re.compile(r'\[Worker\].*Generated\s+(\d+)\s+bytes', re.IGNORECASE)

    result["worker_C_tasks"] = sum(1 for line in lines if pattern_C_finish.search(line))
    result["worker_D_tasks"] = sum(1 for line in lines if pattern_D_finish.search(line))
    result["worker_F_tasks"] = sum(1 for line in lines if pattern_F_finish.search(line))

    # Sum all bytes generated
    total_bytes = 0
    for line in lines:
        m = pattern_bytes.search(line)
        if m:
            total_bytes += int(m.group(1))
    if total_bytes > 0:
        result["total_bytes"] = total_bytes

    # --- Calculate timing from timestamps ---
    # Find first worker task pulled and last worker task finished
    pattern_pulled = re.compile(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}).*\[WorkerLoop\].*Pulled task test-strategyB-getnext', re.IGNORECASE)
    pattern_finished = re.compile(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}).*\[WorkerLoop\].*Finished task test-strategyB-getnext', re.IGNORECASE)
    
    pulled_times = []
    finished_times = []
    
    for line in lines:
        m = pattern_pulled.search(line)
        if m:
            ts = parse_timestamp(line)
            if ts:
                pulled_times.append(ts)
        
        m = pattern_finished.search(line)
        if m:
            ts = parse_timestamp(line)
            if ts:
                finished_times.append(ts)
    
    if pulled_times and finished_times:
        first_pull = min(pulled_times)
        last_finish = max(finished_times)
        total_ms = (last_finish - first_pull).total_seconds() * 1000
        result["total_ms"] = round(total_ms, 2)
        
        # Calculate throughput if we have bytes and time
        if result["total_bytes"] and total_ms > 0:
            throughput_MBps = (result["total_bytes"] / (1024 * 1024)) / (total_ms / 1000)
            result["throughput_MBps"] = round(throughput_MBps, 2)

    # --- Classify status ---
    if result["total_chunks"] is not None and result["total_chunks"] > 0:
        result["status"] = "ok"
    elif result["worker_C_tasks"] or result["worker_D_tasks"] or result["worker_F_tasks"]:
        result["status"] = "server_only"
    else:
        result["status"] = "empty"

    return result

# ---------- Main ----------

def main() -> None:
    parser = argparse.ArgumentParser(description="Extract Strategy B metrics from combined server logs (6 nodes: A,B,C,D,E,F).")
    parser.add_argument(
        "--glob",
        default="logs/logs_*.txt",
        help="Glob pattern for log files (default: logs/logs_*.txt)",
    )
    parser.add_argument(
        "--output",
        default="results/mini3_metrics.csv",
        help="Output CSV path (default: results/mini3_metrics.csv)",
    )
    args = parser.parse_args()

    root = Path(".")
    log_files = sorted(root.glob(args.glob))
    if not log_files:
        print(f"No log files matched pattern: {args.glob}")
        return

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    rows: List[Dict[str, Any]] = []
    for lf in log_files:
        try:
            print(f"Processing {lf.name}...")
            r = parse_log_file(lf)
            rows.append(r)
            # Print summary
            print(f"  Dataset: {r['dataset']}, Rows: {r['rows']}, Scenario: {r['scenario']}, Status: {r['status']}")
            print(f"  Chunks: {r['total_chunks']}, Bytes: {r['total_bytes']}, Time: {r['total_ms']}ms")
            print(f"  Worker tasks - C:{r['worker_C_tasks']}, D:{r['worker_D_tasks']}, F:{r['worker_F_tasks']}")
        except Exception as e:
            print(f"ERROR parsing {lf}: {e}")

    fieldnames = [
        "file",
        "dataset",
        "rows",
        "scenario",
        "status",
        "ttfb_ms",
        "total_ms",
        "total_chunks",
        "total_bytes",
        "throughput_MBps",
        "worker_C_tasks",
        "worker_D_tasks",
        "worker_F_tasks",
    ]

    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in rows:
            writer.writerow(r)

    print(f"\n✅ Wrote {len(rows)} rows to {out_path}")


if __name__ == "__main__":
    main()
