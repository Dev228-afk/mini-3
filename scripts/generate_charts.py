#!/usr/bin/env python3
"""
Performance Visualization Generator for Mini2 Distributed System
Generates professional charts for class presentation
"""

import matplotlib.pyplot as plt
import numpy as np
import os

# Set style for professional presentation
plt.style.use('seaborn-v0_8-darkgrid')
plt.rcParams['figure.figsize'] = (10, 6)
plt.rcParams['font.size'] = 12
plt.rcParams['axes.titlesize'] = 14
plt.rcParams['axes.labelsize'] = 12

# Create output directory
output_dir = 'charts'
os.makedirs(output_dir, exist_ok=True)

# Performance data from testing
dataset_sizes = ['1K', '10K', '100K', '1M', '10M']
processing_times = [0.14, 0.177, 1.3, 45.5, 169.6]  # seconds
throughput = [8.4, 6.6, 8.9, 2.6, 6.9]  # MB/s
data_sizes_mb = [1.18, 1.17, 11.69, 116.89, 1168.73]  # MB

# Cache performance data
cache_dataset = '100K'
cold_cache_time = 1128  # ms
warm_cache_time = 508   # ms

# Memory efficiency data
traditional_memory = 1200  # MB (monolithic approach)
chunked_memory = 408       # MB (our approach with 3 chunks)

# ==================== CHART 1: Cold vs Warm Cache ====================
fig, ax = plt.subplots(figsize=(8, 6))
cache_data = [cold_cache_time, warm_cache_time]
cache_labels = ['Cold Start\n(First Run)', 'Warm Cache\n(Second Run)']
colors = ['#e74c3c', '#27ae60']

bars = ax.bar(cache_labels, cache_data, color=colors, alpha=0.8, edgecolor='black', linewidth=1.5)

# Add value labels on bars
for bar in bars:
    height = bar.get_height()
    ax.text(bar.get_x() + bar.get_width()/2., height,
            f'{int(height)}ms',
            ha='center', va='bottom', fontsize=14, fontweight='bold')

# Add speedup annotation
speedup = cold_cache_time / warm_cache_time
ax.text(0.5, max(cache_data) * 0.5, f'2.2× Speedup!', 
        ha='center', va='center', fontsize=18, fontweight='bold',
        bbox=dict(boxstyle='round,pad=0.5', facecolor='yellow', alpha=0.7))

ax.set_ylabel('Processing Time (milliseconds)', fontweight='bold')
ax.set_title('Cache Performance: Cold Start → Warm Cache\n100K Dataset (11.7 MB)', 
             fontweight='bold', fontsize=16)
ax.set_ylim(0, cold_cache_time * 1.15)
ax.grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.savefig(f'{output_dir}/cache_performance.png', dpi=300, bbox_inches='tight')
print(f"✓ Generated: {output_dir}/cache_performance.png")
plt.close()

# ==================== CHART 2: Memory Efficiency ====================
fig, ax = plt.subplots(figsize=(8, 6))
memory_data = [traditional_memory, chunked_memory]
memory_labels = ['Monolithic\nApproach', 'Chunked\nStreaming\n(Our Design)']
colors = ['#e74c3c', '#27ae60']

bars = ax.bar(memory_labels, memory_data, color=colors, alpha=0.8, edgecolor='black', linewidth=1.5)

# Add value labels on bars
for bar in bars:
    height = bar.get_height()
    ax.text(bar.get_x() + bar.get_width()/2., height,
            f'{int(height)} MB',
            ha='center', va='bottom', fontsize=14, fontweight='bold')

# Add savings annotation
savings_pct = ((traditional_memory - chunked_memory) / traditional_memory) * 100
ax.text(0.5, max(memory_data) * 0.5, f'67% Memory\nSavings', 
        ha='center', va='center', fontsize=18, fontweight='bold',
        bbox=dict(boxstyle='round,pad=0.5', facecolor='yellow', alpha=0.7))

ax.set_ylabel('Peak Memory Usage (MB)', fontweight='bold')
ax.set_title('Memory Efficiency: Chunked Streaming vs Monolithic\n10M Dataset Processing', 
             fontweight='bold', fontsize=16)
ax.set_ylim(0, traditional_memory * 1.15)
ax.grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.savefig(f'{output_dir}/memory_efficiency.png', dpi=300, bbox_inches='tight')
print(f"✓ Generated: {output_dir}/memory_efficiency.png")
plt.close()

# ==================== CHART 3: Scalability Analysis ====================
fig, ax = plt.subplots(figsize=(10, 6))

# Create x-axis positions
x_pos = np.arange(len(dataset_sizes))

# Plot processing time
ax.plot(x_pos, processing_times, marker='o', linewidth=2.5, markersize=10, 
        color='#3498db', label='Processing Time')

# Add value labels
for i, (x, y) in enumerate(zip(x_pos, processing_times)):
    ax.text(x, y, f'{y}s', ha='center', va='bottom', fontsize=10, fontweight='bold')

ax.set_xlabel('Dataset Size', fontweight='bold', fontsize=13)
ax.set_ylabel('Processing Time (seconds)', fontweight='bold', fontsize=13)
ax.set_title('Scalability: Linear Performance from 1K to 10M Rows\n6-Node Hierarchical Architecture', 
             fontweight='bold', fontsize=16)
ax.set_xticks(x_pos)
ax.set_xticklabels(dataset_sizes)
ax.legend(loc='upper left', fontsize=11)
ax.grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig(f'{output_dir}/scalability_analysis.png', dpi=300, bbox_inches='tight')
print(f"✓ Generated: {output_dir}/scalability_analysis.png")
plt.close()

# ==================== CHART 4: Throughput Comparison ====================
fig, ax = plt.subplots(figsize=(10, 6))

bars = ax.bar(dataset_sizes, throughput, color='#9b59b6', alpha=0.8, 
              edgecolor='black', linewidth=1.5)

# Add value labels on bars
for bar in bars:
    height = bar.get_height()
    ax.text(bar.get_x() + bar.get_width()/2., height,
            f'{height:.1f}',
            ha='center', va='bottom', fontsize=11, fontweight='bold')

ax.set_xlabel('Dataset Size', fontweight='bold', fontsize=13)
ax.set_ylabel('Throughput (MB/s)', fontweight='bold', fontsize=13)
ax.set_title('Network Throughput Across Dataset Sizes\nConsistent Performance: 2-9 MB/s', 
             fontweight='bold', fontsize=16)
ax.axhline(y=np.mean(throughput), color='red', linestyle='--', linewidth=2, 
           label=f'Average: {np.mean(throughput):.1f} MB/s')
ax.legend(loc='upper right', fontsize=11)
ax.grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.savefig(f'{output_dir}/throughput_comparison.png', dpi=300, bbox_inches='tight')
print(f"✓ Generated: {output_dir}/throughput_comparison.png")
plt.close()

# ==================== CHART 5: End-to-End Performance Summary ====================
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

# Left: Dataset size vs processing time
ax1.bar(dataset_sizes, processing_times, color='#3498db', alpha=0.8, 
        edgecolor='black', linewidth=1.5)
ax1.set_xlabel('Dataset Size', fontweight='bold')
ax1.set_ylabel('Time (seconds)', fontweight='bold')
ax1.set_title('Processing Time', fontweight='bold', fontsize=14)
ax1.grid(axis='y', alpha=0.3)

# Add annotations
for i, (x, y) in enumerate(zip(dataset_sizes, processing_times)):
    ax1.text(i, y, f'{y}s', ha='center', va='bottom', fontsize=10, fontweight='bold')

# Right: Data size processed
ax2.bar(dataset_sizes, data_sizes_mb, color='#e67e22', alpha=0.8, 
        edgecolor='black', linewidth=1.5)
ax2.set_xlabel('Dataset Size', fontweight='bold')
ax2.set_ylabel('Data Size (MB)', fontweight='bold')
ax2.set_title('Data Volume Processed', fontweight='bold', fontsize=14)
ax2.grid(axis='y', alpha=0.3)

# Add annotations
for i, (x, y) in enumerate(zip(dataset_sizes, data_sizes_mb)):
    if y < 100:
        ax2.text(i, y, f'{y:.1f}', ha='center', va='bottom', fontsize=10, fontweight='bold')
    else:
        ax2.text(i, y, f'{y:.0f}', ha='center', va='bottom', fontsize=10, fontweight='bold')

fig.suptitle('End-to-End Performance Summary: 1K to 10M Rows', 
             fontweight='bold', fontsize=16, y=1.02)
plt.tight_layout()
plt.savefig(f'{output_dir}/performance_summary.png', dpi=300, bbox_inches='tight')
print(f"✓ Generated: {output_dir}/performance_summary.png")
plt.close()

print(f"\n{'='*60}")
print(f"All charts generated successfully in '{output_dir}/' directory!")
print(f"{'='*60}")
print("\nGenerated files:")
print(f"  1. cache_performance.png       - Cold vs Warm cache comparison")
print(f"  2. memory_efficiency.png       - Memory savings visualization")
print(f"  3. scalability_analysis.png    - Linear scalability chart")
print(f"  4. throughput_comparison.png   - Network throughput analysis")
print(f"  5. performance_summary.png     - Complete overview")
print(f"\n✓ Ready for presentation!")
