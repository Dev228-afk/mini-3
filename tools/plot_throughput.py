#!/usr/bin/env python3
"""
Plot throughput vs dataset size from logggg_metrics.csv
Generates a chart matching the "Mixed workload: dataset size vs throughput" format
"""

import pandas as pd
import matplotlib.pyplot as plt
import sys
from pathlib import Path

def plot_throughput(csv_path: str, output_path: str = None):
    """
    Create throughput vs dataset size plot
    
    Args:
        csv_path: Path to the metrics CSV file
        output_path: Optional path to save the plot (if None, displays interactively)
    """
    # Read the CSV
    df = pd.read_csv(csv_path)
    
    # Filter out rows with missing throughput or extreme outliers
    df = df.dropna(subset=['throughput_MBps'])
    
    # Remove the 1k entries (they have anomalously low throughput)
    df = df[df['rows'] != 1000]
    
    # Sort by rows for proper line plotting
    df = df.sort_values('rows')
    
    # Create size labels (100k, 200k, 500k, 1M, 5M, 10M)
    def format_size(rows):
        if rows >= 1_000_000:
            return f"{int(rows / 1_000_000)}M"
        elif rows >= 1_000:
            return f"{int(rows / 1_000)}k"
        else:
            return str(rows)
    
    df['size_label'] = df['rows'].apply(format_size)
    
    # Create the plot
    plt.figure(figsize=(10, 6))
    
    # Plot line with markers
    plt.plot(df['size_label'], df['throughput_MBps'], 
             marker='o', markersize=8, linewidth=2, color='#1f77b4')
    
    # Customize the plot
    plt.title('Mixed workload: dataset size vs throughput', fontsize=14, pad=20)
    plt.xlabel('Dataset (5 concurrent clients, different sizes)', fontsize=11)
    plt.ylabel('Throughput per client (MB/s)', fontsize=11)
    
    # Add grid
    plt.grid(True, linestyle='--', alpha=0.3)
    
    # Set y-axis limits to match the image
    plt.ylim(0, max(df['throughput_MBps']) * 1.1)
    
    # Adjust layout
    plt.tight_layout()
    
    # Save or display
    if output_path:
        plt.savefig(output_path, dpi=300, bbox_inches='tight')
        print(f"Plot saved to {output_path}")
    else:
        plt.show()
    
    # Print the data points
    print("\nData points:")
    print("="*60)
    for _, row in df.iterrows():
        print(f"{row['size_label']:>6} ({row['rows']:>10,} rows): {row['throughput_MBps']:>6.2f} MB/s")


if __name__ == '__main__':
    # Default paths
    csv_file = 'results/logggg_metrics.csv'
    output_file = 'results/throughput_chart.png'
    
    # Allow command line arguments
    if len(sys.argv) > 1:
        csv_file = sys.argv[1]
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
    
    # Check if CSV exists
    if not Path(csv_file).exists():
        print(f"Error: CSV file not found: {csv_file}")
        sys.exit(1)
    
    # Generate the plot
    plot_throughput(csv_file, output_file)
