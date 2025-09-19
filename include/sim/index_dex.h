#pragma once
#include "sim/index.h"
#include <vector>
#include <cstdint>

#include "sim/config.h"

struct Dex : Index {
  DexConf conf;     // store by value
  LRUCache cache;
  struct MsQueue { SimTime ready_at{0.0}; double budget_ops_per_s{3'000'000}; } msq;
  int cs_total{1};

  // partitions
  std::vector<int> bucket_owner; // size=num_partitions; owner CS

  Dex(const IndexCtx& c, DexConf dx, std::size_t cache_bytes, int cs_total_, double ms_budget_ops_per_s);
  void get(std::uint64_t key, Metrics& m, std::uint64_t op_id) override;
  void put(std::uint64_t key, Metrics& m, std::uint64_t op_id) override;
private:
  int owner_cs(std::uint64_t key) const;
  SimTime offload_cost_est(int range_len) const;
  SimTime onesided_cost_est(int misses, std::size_t bytes) const;

  // advanced helpers
  void init_partitions();
  void schedule_repartition();
  void do_repartition();
  int bucket_of(std::uint64_t key) const;
};
