#include "sim/workload.h"
#include "sim/config.h"
#include "sim/index_sherman.h"
#include <random>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

WorkloadRunner::WorkloadRunner(const SimConf& c)
  : conf(c),
    nic(loop, NIC::Caps{
      c.nic.link_gbps, c.nic.base_rtt_us, c.nic.per_byte_us, c.nic.cas_onchip_rtt_us,
      c.nic.in_order_rc, c.nic.qp_per_thread,
      c.nic.small_threshold, c.nic.doorbell_batch_limit, c.nic.pcie_doorbell_us, c.nic.pcie_desc_us, c.nic.sq_depth,
      c.nic.tb_cas_ops_per_s, c.nic.tb_read_ops_per_s, c.nic.tb_write_ops_per_s, c.nic.tb_burst_ops
    }) {
  metrics.trace_enabled = conf.metrics.dump_per_op_trace;
}

std::unique_ptr<Index> WorkloadRunner::make_index_for_cs(int cs_id, int ms_id, int qp, std::size_t cache_bytes){
  IndexCtx ctx{&loop, &nic, cs_id, ms_id, qp, conf.index.node_bytes, conf.index.leaf_entry_bytes};
  auto sh = conf.index.sh; // copy
  // apply ablations
  if (conf.index.ablations.sherman.disable_combine)  sh.combine = false;
  if (conf.index.ablations.sherman.disable_hocl)     sh.hocl.enable = false;
  if (conf.index.ablations.sherman.disable_versions) { sh.enable_two_level_versions = false; sh.two_level_versioning = false; }
  return std::make_unique<Sherman>(ctx, sh, cache_bytes);
}

void WorkloadRunner::run_workload(const WorkloadCfg& wl, const std::string& index_name, const std::string& out_dir){
  fs::create_directories(out_dir);
  // reset loop and metrics per workload
  loop = EventLoop{}; metrics.reset(); metrics.trace_enabled = conf.metrics.dump_per_op_trace;
  if (metrics.trace_enabled) metrics.open_trace(out_dir+"/op_trace_"+wl.name+"_"+index_name+".csv");

  const int CS = conf.cluster.compute_nodes;
  const int TP = conf.cluster.threads_per_compute;
  indices.clear(); indices.reserve(CS*TP);
  for (int cs=0; cs<CS; ++cs)
    for (int th=0; th<TP; ++th)
      indices.push_back(make_index_for_cs(cs, /*ms=*/cs % conf.cluster.memory_nodes, /*qp=*/th, conf.cluster.cs_cache_bytes));

  Zipf zipf(wl.keyspace, wl.zipf);
  std::mt19937_64 rng(42);
  std::uniform_real_distribution<double> U(0.0,1.0);

  for (std::size_t i=0;i<wl.ops;i++){
    auto& idx = indices[i % indices.size()];
    bool is_read = (U(rng) < wl.mix.read);
    std::uint64_t key = zipf.sample(U(rng));
    loop.after(0, [&, is_read, key, idx_ptr=idx.get(), op_id=i](){
      if(is_read) idx_ptr->get(key, metrics, op_id);
      else idx_ptr->put(key, metrics, op_id);
    });
  }
  loop.run();

  // summary CSV append
  fs::create_directories(out_dir);
  const std::string sum_path = out_dir+"/metrics_summary.csv";
  const bool exists = fs::exists(sum_path);
  std::ofstream out(sum_path, std::ios::app);
  if (!exists) out << "index,workload,ops,p50_us,p95_us,p99_us,reads,writes,cas,sends,recvs,bytes_r,bytes_w\n";
  // percentiles
  double p50=0, p95=0, p99=0;
  {
    std::lock_guard<std::mutex> g(metrics.lat_m);
    p50 = metrics.lat_us.pct(50);
    p95 = metrics.lat_us.pct(95);
    p99 = metrics.lat_us.pct(99);
  }
  out << index_name << ',' << wl.name << ',' << metrics.ops.load() << ','
      << p50 << ',' << p95 << ',' << p99 << ','
      << metrics.remote_reads.load() << ',' << metrics.remote_writes.load() << ',' << metrics.remote_cas.load() << ','
      << metrics.send_ops.load() << ',' << metrics.recv_ops.load() << ','
      << metrics.bytes_read.load() << ',' << metrics.bytes_write.load() << "\n";
}
