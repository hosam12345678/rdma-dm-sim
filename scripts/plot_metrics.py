# Usage: python3 scripts/plot_metrics.py out/metrics_summary.csv
import sys, pandas as pd, matplotlib.pyplot as plt

if len(sys.argv)<2:
    print("usage: plot_metrics.py out/metrics_summary.csv"); sys.exit(1)

df = pd.read_csv(sys.argv[1])
for wl, g in df.groupby('workload'):
    fig = plt.figure()
    ax = fig.add_subplot(111)
    x = g['index']
    ax.bar(x, g['ops'])
    ax.set_title(f"Throughput (ops) — {wl}")
    ax.set_ylabel('ops completed')
    fig.tight_layout()
    fig.savefig(f"{wl}_throughput.png", dpi=150)

    fig2 = plt.figure()
    ax2 = fig2.add_subplot(111)
    ax2.bar(x, g['p99_us'])
    ax2.set_title(f"p99 latency (us) — {wl}")
    ax2.set_ylabel('microseconds')
    fig2.tight_layout()
    fig2.savefig(f"{wl}_p99.png", dpi=150)
print("wrote *png files next to metrics_summary.csv")
