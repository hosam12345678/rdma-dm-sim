#include "sim/config.h"
#include <yaml-cpp/yaml.h>
#include <stdexcept>

static IndexKind ParseIndexKind(const std::string& s){
  if (s=="sherman") return IndexKind::Sherman;
  if (s=="dex") return IndexKind::DEX;
  throw std::runtime_error("index.kind must be 'sherman' or 'dex'");
}

SimConf LoadConfig(const std::string& path){
  YAML::Node y = YAML::LoadFile(path);
  SimConf c;
  // cluster
  if (auto cl = y["cluster"]; cl){
    c.cluster.compute_nodes = cl["compute_nodes"].as<int>(c.cluster.compute_nodes);
    c.cluster.memory_nodes  = cl["memory_nodes"].as<int>(c.cluster.memory_nodes);
    c.cluster.threads_per_compute = cl["threads_per_compute"].as<int>(c.cluster.threads_per_compute);
    c.cluster.cs_cache_bytes = cl["cs_cache_bytes"].as<std::size_t>(c.cluster.cs_cache_bytes);
    c.cluster.ms_cpu_cores = cl["ms_cpu_cores"].as<int>(c.cluster.ms_cpu_cores);
  }
  // nic
  if (auto nic = y["nic"]; nic){
    c.nic.link_gbps = nic["link_gbps"].as<double>(c.nic.link_gbps);
    c.nic.base_rtt_us = nic["base_rtt_us"].as<double>(c.nic.base_rtt_us);
    c.nic.per_byte_us = nic["per_byte_us"].as<double>(c.nic.per_byte_us);
    c.nic.cas_onchip_rtt_us = nic["cas_onchip_rtt_us"].as<double>(c.nic.cas_onchip_rtt_us);
    if (auto iops = nic["iops_caps_per_qp"]) {
      c.nic.iops_cas = iops["cas"].as<std::uint64_t>(c.nic.iops_cas);
      c.nic.iops_read_small = iops["read_small"].as<std::uint64_t>(c.nic.iops_read_small);
      c.nic.iops_write_small = iops["write_small"].as<std::uint64_t>(c.nic.iops_write_small);
    }
    c.nic.qp_per_thread = nic["qp_per_thread"].as<int>(c.nic.qp_per_thread);
    c.nic.in_order_rc = nic["in_order_rc"].as<bool>(c.nic.in_order_rc);

    // advanced NIC knobs
    c.nic.tb_cas_ops_per_s   = nic["tb_cas_ops_per_s"].as<double>(c.nic.tb_cas_ops_per_s);
    c.nic.tb_read_ops_per_s  = nic["tb_read_ops_per_s"].as<double>(c.nic.tb_read_ops_per_s);
    c.nic.tb_write_ops_per_s = nic["tb_write_ops_per_s"].as<double>(c.nic.tb_write_ops_per_s);
    c.nic.tb_burst_ops       = nic["tb_burst_ops"].as<double>(c.nic.tb_burst_ops);
    c.nic.small_threshold    = nic["small_threshold"].as<std::size_t>(c.nic.small_threshold);
    c.nic.pcie_doorbell_us   = nic["pcie_doorbell_us"].as<double>(c.nic.pcie_doorbell_us);
    c.nic.pcie_desc_us       = nic["pcie_desc_us"].as<double>(c.nic.pcie_desc_us);
    c.nic.doorbell_batch_limit = nic["doorbell_batch_limit"].as<int>(c.nic.doorbell_batch_limit);
    c.nic.sq_depth           = nic["sq_depth"].as<int>(c.nic.sq_depth);
  }
  // memory_server
  if (auto ms = y["memory_server"]; ms){
    c.mem.onchip_bytes = ms["rnic_onchip_bytes"].as<std::size_t>(c.mem.onchip_bytes);
    c.mem.dram_lat_us = ms["dram_latency_us"].as<double>(c.mem.dram_lat_us);
  }
  // index
  if (auto idx = y["index"]; idx){
    c.index.kind = ParseIndexKind(idx["kind"].as<std::string>("sherman"));
    c.index.node_bytes = idx["node_bytes"].as<std::size_t>(c.index.node_bytes);
    c.index.leaf_entry_bytes = idx["leaf_entry_bytes"].as<std::size_t>(c.index.leaf_entry_bytes);
    // ablations
    if (auto abl = idx["ablations"]){
      if (auto sh = abl["sherman"]){
        c.index.ablations.sherman.disable_combine  = sh["disable_combine"].as<bool>(c.index.ablations.sherman.disable_combine);
        c.index.ablations.sherman.disable_hocl     = sh["disable_hocl"].as<bool>(c.index.ablations.sherman.disable_hocl);
        c.index.ablations.sherman.disable_versions = sh["disable_versions"].as<bool>(c.index.ablations.sherman.disable_versions);
      }
      if (auto dx = abl["dex"]){
        c.index.ablations.dex.disable_partitioning = dx["disable_partitioning"].as<bool>(c.index.ablations.dex.disable_partitioning);
        c.index.ablations.dex.disable_path_cache   = dx["disable_path_cache"].as<bool>(c.index.ablations.dex.disable_path_cache);
        c.index.ablations.dex.disable_offload      = dx["disable_offload"].as<bool>(c.index.ablations.dex.disable_offload);
      }
    }
  }
  // sherman
  if (auto sh = y["sherman"]; sh){
    c.index.sh.combine = sh["combine_commands"].as<bool>(c.index.sh.combine);
    if (auto hocl = sh["hocl"]) {
      c.index.sh.hocl.enable    = hocl["enable"].as<bool>(c.index.sh.hocl.enable);
      c.index.sh.hocl.glt_slots = hocl["glt_slots"].as<int>(c.index.sh.hocl.glt_slots);
      c.index.sh.hocl.llt_enable= hocl["llt_enable"].as<bool>(c.index.sh.hocl.llt_enable);
    }
    c.index.sh.two_level_versioning = sh["two_level_versioning"].as<bool>(c.index.sh.two_level_versioning);
    c.index.sh.cache_levels = sh["cache_levels"].as<int>(c.index.sh.cache_levels);
    // advanced
    c.index.sh.glt_hash_seed = sh["glt_hash_seed"].as<int>(c.index.sh.glt_hash_seed);
    c.index.sh.cas_max_retries = sh["cas_max_retries"].as<int>(c.index.sh.cas_max_retries);
    c.index.sh.cas_backoff_us = sh["cas_backoff_us"].as<double>(c.index.sh.cas_backoff_us);
    c.index.sh.model_glt_collisions = sh["model_glt_collisions"].as<bool>(c.index.sh.model_glt_collisions);
    c.index.sh.leaf_max_entries = sh["leaf_max_entries"].as<int>(c.index.sh.leaf_max_entries);
    c.index.sh.split_threshold = sh["split_threshold"].as<double>(c.index.sh.split_threshold);
    c.index.sh.merge_threshold = sh["merge_threshold"].as<double>(c.index.sh.merge_threshold);
    c.index.sh.enable_splits = sh["enable_splits"].as<bool>(c.index.sh.enable_splits);
    c.index.sh.enable_merges = sh["enable_merges"].as<bool>(c.index.sh.enable_merges);
    c.index.sh.enable_two_level_versions = sh["enable_two_level_versions"].as<bool>(c.index.sh.enable_two_level_versions);
  }
  // dex
  if (auto dx = y["dex"]; dx){
    c.index.dx.logical_partitioning = dx["logical_partitioning"].as<bool>(c.index.dx.logical_partitioning);
    c.index.dx.path_aware_cache = dx["path_aware_cache"].as<bool>(c.index.dx.path_aware_cache);
    if (auto off = dx["offload"]) {
      c.index.dx.offload.enable = off["enable"].as<bool>(c.index.dx.offload.enable);
      c.index.dx.offload.ms_cpu_budget_ops_per_s = off["ms_cpu_budget_ops_per_s"].as<double>(c.index.dx.offload.ms_cpu_budget_ops_per_s);
    }
    // advanced
    c.index.dx.num_partitions = dx["num_partitions"].as<int>(c.index.dx.num_partitions);
    c.index.dx.repartition_period_ms = dx["repartition_period_ms"].as<double>(c.index.dx.repartition_period_ms);
    c.index.dx.repartition_topK = dx["repartition_topK"].as<int>(c.index.dx.repartition_topK);
    c.index.dx.remap_broadcast_us = dx["remap_broadcast_us"].as<double>(c.index.dx.remap_broadcast_us);
    c.index.dx.cache_inval_prob = dx["cache_inval_prob"].as<double>(c.index.dx.cache_inval_prob);
  }
  // workloads
  if (auto wls = y["workloads"]; wls){
    for (auto w : wls) {
      WorkloadCfg wc;
      wc.name      = w["name"].as<std::string>("workload");
      wc.ops       = w["ops"].as<std::size_t>(0);
      if (auto mix = w["mix"]) { wc.mix.read = mix["read"].as<double>(1.0); wc.mix.write = mix["write"].as<double>(0.0); }
      wc.keyspace  = w["keyspace"].as<std::uint64_t>(0);
      wc.zipf      = w["zipf"].as<double>(0.0);
      wc.range_len = w["range_len"].as<std::uint32_t>(1);
      c.workloads.push_back(wc);
    }
  }
  // metrics
  if (auto m = y["metrics"]; m){
    c.metrics.dump_per_op_trace = m["dump_per_op_trace"].as<bool>(c.metrics.dump_per_op_trace);
    c.metrics.out_dir = m["out_dir"].as<std::string>(c.metrics.out_dir);
    if (auto p = m["ptiles"]) { c.metrics.ptiles.clear(); for (auto v: p) c.metrics.ptiles.push_back(v.as<int>()); }
  }
  return c;
}
