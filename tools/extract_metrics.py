#!/usr/bin/env python3
"""
Extract metrics from combined logs_testdata_*.txt files and produce metrics CSV.
"""

import re
import glob
import csv
import os
from datetime import datetime
from pathlib import Path


def parse_timestamp(line: str) -> datetime:
    """Extract timestamp from log line prefix: YYYY-MM-DD HH:MM:SS.mmm"""
    match = re.match(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})', line)
    if match:
        return datetime.strptime(match.group(1), '%Y-%m-%d %H:%M:%S.%f')
    return None


def extract_float_or_ms(text: str) -> int:
    """Convert '11.623 s' to 11623 or '15346 ms' to 15346"""
    match = re.search(r'([\d.]+)\s*(s|ms)', text)
    if match:
        value = float(match.group(1))
        unit = match.group(2)
        if unit == 's':
            return int(value * 1000)
        else:  # ms
            return int(value)
    return None


def extract_dataset_info(content: str, filename: str):
    """Extract dataset name and row count from content or filename"""
    # Try to extract from PROCESSING DATASET line
    dataset_match = re.search(r'PROCESSING DATASET:\s*test_data/(data_\w+\.csv)', content)
    if dataset_match:
        dataset = dataset_match.group(1)
    else:
        # Fall back to filename
        fname_match = re.search(r'logs_testdata_(\w+)\.txt', filename)
        if fname_match:
            dataset = f"data_{fname_match.group(1)}.csv"
        else:
            dataset = "unknown.csv"
    
    # Convert dataset name to row count
    rows_map = {
        '1k': 1000,
        '5k': 5000,
        '10k': 10000,
        '50k': 50000,
        '100k': 100000,
        '200k': 200000,
        '500k': 500000,
        '1m': 1000000,
        '5m': 5000000,
        '10m': 10000000
    }
    
    for key, value in rows_map.items():
        if key in dataset.lower():
            return dataset, value
    
    return dataset, 0


def parse_team_metrics(content: str, team_leader: str, workers: list) -> dict:
    """Parse metrics for a single team (B+C or E+D/F)"""
    metrics = {
        'leader_ms': None,
        'worker_window_ms': None,
        'worker_tasks': {w: 0 for w in workers}
    }
    
    # Find team leader start and end
    leader_start = None
    leader_end = None
    
    for line in content.split('\n'):
        if f'[TeamLeader {team_leader}]' in line and 'HandleTeamRequest' in line and 'test-strategyB-getnext' in line:
            ts = parse_timestamp(line)
            if ts and leader_start is None:
                leader_start = ts
        
        if f'[TeamLeader {team_leader}]' in line and 'Received all 3 results' in line and 'test-strategyB-getnext' in line:
            ts = parse_timestamp(line)
            if ts:
                leader_end = ts
    
    if leader_start and leader_end:
        metrics['leader_ms'] = int((leader_end - leader_start).total_seconds() * 1000)
    
    # Find worker window (first pull to last finish)
    worker_pulls = []
    worker_finishes = []
    
    for line in content.split('\n'):
        for worker in workers:
            if f'[{worker}]' in line and '[WorkerLoop] Pulled task test-strategyB-getnext' in line:
                ts = parse_timestamp(line)
                if ts:
                    worker_pulls.append(ts)
            
            if f'[{worker}]' in line and '[WorkerLoop] Finished task test-strategyB-getnext' in line:
                ts = parse_timestamp(line)
                if ts:
                    worker_finishes.append(ts)
                # Count tasks per worker
                metrics['worker_tasks'][worker] += 1
    
    if worker_pulls and worker_finishes:
        first_pull = min(worker_pulls)
        last_finish = max(worker_finishes)
        metrics['worker_window_ms'] = int((last_finish - first_pull).total_seconds() * 1000)
    
    return metrics


def parse_client_metrics(content: str) -> dict:
    """Parse client summary metrics (TTFB, total time, chunks)"""
    metrics = {
        'ttfb_ms': None,
        'total_ms': None,
        'total_chunks': None
    }
    
    # Find Strategy B section
    strategy_b_section = re.search(
        r'Strategy B \(GetNext\) Results:.*?(?=\n\n|\Z)',
        content,
        re.DOTALL
    )
    
    if strategy_b_section:
        section_text = strategy_b_section.group(0)
        
        # Extract TTFB
        ttfb_match = re.search(r'Time to first chunk:\s*([\d.]+\s*(?:s|ms))', section_text)
        if ttfb_match:
            metrics['ttfb_ms'] = extract_float_or_ms(ttfb_match.group(1))
        
        # Extract total time
        total_match = re.search(r'Total time:\s*([\d.]+\s*(?:s|ms))', section_text)
        if total_match:
            metrics['total_ms'] = extract_float_or_ms(total_match.group(1))
        
        # Extract total chunks
        chunks_match = re.search(r'Total chunks:\s*(\d+)', section_text)
        if chunks_match:
            metrics['total_chunks'] = int(chunks_match.group(1))
    
    return metrics


def process_log_file(filepath: str) -> list:
    """Process a single log file and return list of metric rows"""
    rows = []
    
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    # Extract dataset info
    dataset, row_count = extract_dataset_info(content, os.path.basename(filepath))
    
    # Parse client metrics (shared by both teams)
    client_metrics = parse_client_metrics(content)
    
    # Parse green team (B + C)
    green_metrics = parse_team_metrics(content, 'B', ['C'])
    rows.append({
        'dataset': dataset,
        'rows': row_count,
        'team': 'green',
        'leader_ms': green_metrics['leader_ms'],
        'worker_window_ms': green_metrics['worker_window_ms'],
        'ttfb_ms': client_metrics['ttfb_ms'],
        'total_ms': client_metrics['total_ms'],
        'total_chunks': client_metrics['total_chunks'],
        'worker_C_tasks': green_metrics['worker_tasks']['C'],
        'worker_D_tasks': 0,
        'worker_F_tasks': 0
    })
    
    # Parse pink team (E + D + F)
    pink_metrics = parse_team_metrics(content, 'E', ['D', 'F'])
    rows.append({
        'dataset': dataset,
        'rows': row_count,
        'team': 'pink',
        'leader_ms': pink_metrics['leader_ms'],
        'worker_window_ms': pink_metrics['worker_window_ms'],
        'ttfb_ms': client_metrics['ttfb_ms'],
        'total_ms': client_metrics['total_ms'],
        'total_chunks': client_metrics['total_chunks'],
        'worker_C_tasks': 0,
        'worker_D_tasks': pink_metrics['worker_tasks']['D'],
        'worker_F_tasks': pink_metrics['worker_tasks']['F']
    })
    
    return rows


def main():
    """Main entry point"""
    # Find all log files
    log_files = []
    
    # Check current directory
    log_files.extend(glob.glob('logs_testdata_*.txt'))
    
    # Check logs/ directory if it exists
    if os.path.isdir('logs'):
        log_files.extend(glob.glob('logs/logs_testdata_*.txt'))
    
    if not log_files:
        print("No log files found matching pattern logs_testdata_*.txt")
        return
    
    print(f"Found {len(log_files)} log file(s)")
    
    # Process all files
    all_rows = []
    for filepath in sorted(log_files):
        print(f"Processing {filepath}...")
        rows = process_log_file(filepath)
        all_rows.extend(rows)
    
    # Ensure results directory exists
    os.makedirs('results', exist_ok=True)
    
    # Write CSV
    output_path = 'results/metrics_strategyB.csv'
    fieldnames = [
        'dataset',
        'rows',
        'team',
        'leader_ms',
        'worker_window_ms',
        'ttfb_ms',
        'total_ms',
        'total_chunks',
        'worker_C_tasks',
        'worker_D_tasks',
        'worker_F_tasks'
    ]
    
    with open(output_path, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(all_rows)
    
    print(f"\nProcessed {len(log_files)} file(s), wrote {len(all_rows)} row(s) to {output_path}")


if __name__ == '__main__':
    main()
