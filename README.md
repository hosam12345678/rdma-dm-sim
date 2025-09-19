# rdma-dm-sim (advanced + ablations)

A small RDMA-driven distributed-memory index simulator with two pluggable designs (Sherman & DEX).
It includes a YAML config loader, a simple event loop, a NIC model with token buckets & PCIe posting,
LRU cache, and plotting helper. **Ablation flags** under `index.ablations.*` let you flip off
individual techniques for paper-style step experiments.

## Build
```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
```

> Requires `yaml-cpp` (e.g., `sudo apt-get install -y libyaml-cpp-dev` or vcpkg).

## Run
```bash
./sim ../data/sim.yaml
python3 ../scripts/plot_metrics.py out/metrics_summary.csv
```

Outputs:
- `out/metrics_summary.csv` (aggregate per workload & index)
- `out/op_trace_*.csv` (if enabled)
- `{workload}_throughput.png` and `{workload}_p99.png` next to the summary CSV

## Key YAML knobs

### index.ablations.sherman
- `disable_combine`: turn off write-combine chain
- `disable_hocl`: disable HOCL acquire/release path
- `disable_versions`: disable two-level version checks

### index.ablations.dex
- `disable_partitioning`: treat all keys as locally owned (no cross-CS hop)
- `disable_path_cache`: bypass path-aware cache (forces misses)
- `disable_offload`: disable MS offload (use one-sided RDMA only)

### NIC (selected)
- `tb_*_ops_per_s`, `tb_burst_ops` — IOPS token-bucket caps per verb/QP
- `pcie_*`, `doorbell_batch_limit`, `sq_depth` — host posting and queueing

Tune `sherman.*` and `dex.*` sections for deeper fidelity (GLT retries, splits, remap cadence, etc.).
