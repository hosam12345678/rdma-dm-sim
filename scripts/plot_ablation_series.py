# Usage: python3 scripts/plot_ablation_series.py
# Plots and compares all ablation metrics in one go.
import os
import pandas as pd
import matplotlib.pyplot as plt

ABLATION_DIRS = [
    ("FG+", "out/fgplus/metrics_summary.csv"),
    ("+Combine", "out/combine/metrics_summary.csv"),
    ("+OnChip", "out/onchip/metrics_summary.csv"),
    ("+Hier", "out/hier/metrics_summary.csv"),
    ("+2Lvl", "out/2lvl/metrics_summary.csv"),
]

# Collect all data
all_data = []
for label, path in ABLATION_DIRS:
    if os.path.exists(path):
        df = pd.read_csv(path)
        df['ablation'] = label
        all_data.append(df)
    else:
        print(f"Warning: {path} not found, skipping.")

df_all = pd.concat(all_data, ignore_index=True)

# For each workload, plot throughput and p99 across ablations

# Ensure output directories exist
os.makedirs("results", exist_ok=True)


for wl in df_all['workload'].unique():
    g = df_all[df_all['workload'] == wl].copy()
    # Throughput: ops / (p99_us / 1e6) = ops/sec
    g['throughput_ops_sec'] = g['ops'] / (g['p99_us'] / 1e6)

    plt.figure()
    plt.bar(g['ablation'], g['throughput_ops_sec'])
    plt.title(f"Throughput (ops/sec) — {wl}")
    plt.ylabel('ops/sec')
    plt.tight_layout()
    plt.savefig(os.path.join("results", f"{wl}_throughput_ablation.png"), dpi=150)
    plt.close()

    plt.figure()
    plt.bar(g['ablation'], g['p99_us'])
    plt.title(f"p99 latency (us) — {wl}")
    plt.ylabel('microseconds')
    plt.tight_layout()
    plt.savefig(os.path.join("results", f"{wl}_p99_ablation.png"), dpi=150)
    plt.close()

print("Wrote ablation comparison PNGs for each workload.")
