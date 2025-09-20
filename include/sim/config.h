#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Mix { double read{1.0}, write{0.0}; };

struct WorkloadCfg {
  std::string name;
  std::size_t ops{0};
  Mix mix;
  std::uint64_t keyspace{0};
  double zipf{0.0};
  std::uint32_t range_len{1};
};

struct NicCaps {
  double link_gbps{100};
  double base_rtt_us{2.0};
  double per_byte_us{1e-5};
  double cas_onchip_rtt_us{0.7};
  std::uint64_t iops_cas{120'000'000};
  std::uint64_t iops_read_small{8'500'000};
  std::uint64_t iops_write_small{9'000'000};
  bool in_order_rc{true};
  int qp_per_thread{1};

  // Advanced: token buckets & PCIe posting
  double tb_cas_ops_per_s{120e6};
  double tb_read_ops_per_s{8.5e6};
  double tb_write_ops_per_s{9.0e6};
  double tb_burst_ops{64};
  std::size_t small_threshold{256};

  double pcie_doorbell_us{0.25};
  double pcie_desc_us{0.03};
  int doorbell_batch_limit{16};
  int sq_depth{512};
};

struct ClusterConf {
  int compute_nodes{4};
  int memory_nodes{2};
  int threads_per_compute{16};
  std::size_t cs_cache_bytes{256ull*1024*1024};
  int ms_cpu_cores{2};
};

struct MemoryConf { std::size_t onchip_bytes{256*1024}; double dram_lat_us{0.6}; };

enum class IndexKind { Sherman, DEX };

struct ShermanConf {
  bool combine{true};
  struct {
    bool enable{true};
    int glt_slots{131072};
    bool llt_enable{true};
    // Local queueing cost per position in LLT (purely local, no NIC). Keep small but non-zero to model fairness handoff.
    double llt_local_wait_us{0.0};
  } hocl;
  bool two_level_versioning{true};
  int cache_levels{2};

  // RDWC (Read/Write Delegation with Coalescing) - SMART-style
  struct rdwc_t {
    bool enable{false};
    double window_us{100.0}; // delegation window in microseconds
    enum CollisionPolicy { BYPASS = 0, QUEUE = 1 } collision_policy{QUEUE};
  } rdwc;

  // Advanced fidelity
  unsigned int glt_hash_seed{0x9e3779b9};
  int cas_max_retries{16};
  double cas_backoff_us{0.5};
  bool model_glt_collisions{true};

  int leaf_max_entries{-1};
  double split_threshold{0.95};
  double merge_threshold{0.30};
  bool enable_splits{true};
  bool enable_merges{false};
  bool enable_two_level_versions{true};
};

struct DexConf {
  bool logical_partitioning{true};
  bool path_aware_cache{true};
  struct { bool enable{true}; double ms_cpu_budget_ops_per_s{3'000'000}; } offload;

  // Advanced: repartitioning & invalidation
  int num_partitions{256};
  double repartition_period_ms{250.0};
  int repartition_topK{8};
  double remap_broadcast_us{100.0};
  double cache_inval_prob{0.25};
};

// Ablations (paper-style toggles)
struct ShermanAblations { bool disable_combine{false}; bool disable_hocl{false}; bool disable_versions{false}; };
struct DexAblations { bool disable_partitioning{false}; bool disable_path_cache{false}; bool disable_offload{false}; };
struct Ablations { ShermanAblations sherman; DexAblations dex; };

struct IndexConf {
  IndexKind kind{IndexKind::Sherman};
  std::size_t node_bytes{4096};
  std::size_t leaf_entry_bytes{24};
  ShermanConf sh;
  DexConf dx;
  Ablations ablations;
};

struct MetricsCfg { std::vector<int> ptiles{50,95,99}; bool dump_per_op_trace{true}; std::string out_dir{"out"}; };

struct SimConf {
  ClusterConf cluster;
  NicCaps nic;
  MemoryConf mem;
  IndexConf index;
  std::vector<WorkloadCfg> workloads;
  MetricsCfg metrics;
};

SimConf LoadConfig(const std::string& path);