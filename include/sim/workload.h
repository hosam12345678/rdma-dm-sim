#pragma once
#include "sim/config.h"
#include "sim/index.h"
#include "sim/zipf.h"
#include <memory>
#include <vector>

struct WorkloadRunner {
  SimConf conf;
  EventLoop loop;
  NIC nic;
  Metrics metrics;
  std::vector<std::unique_ptr<Index>> indices;
  WorkloadRunner(const SimConf& c);
  std::unique_ptr<Index> make_index_for_cs(int cs_id, int ms_id, int qp, std::size_t cache_bytes);
  void run_workload(const WorkloadCfg& wl, const std::string& index_name, const std::string& out_dir);
};
