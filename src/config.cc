#include "sim/config.h"
#include <yaml-cpp/yaml.h>

SimConf LoadConfig(const std::string& path){
  SimConf c;
  auto y = YAML::LoadFile(path);

  // nic
  if (auto n = y["nic"]; n){
    c.nic.link_gbps = n["link_gbps"].as<double>(c.nic.link_gbps);
    c.nic.base_rtt_us = n["base_rtt_us"].as<double>(c.nic.base_rtt_us);
    c.nic.per_byte_us = n["per_byte_us"].as<double>(c.nic.per_byte_us);
    c.nic.cas_onchip_rtt_us = n["cas_onchip_rtt_us"].as<double>(c.nic.cas_onchip_rtt_us);
    c.nic.iops_cas = n["iops_cas"].as<std::uint64_t>(c.nic.iops_cas);
    c.nic.iops_read_small = n["iops_read_small"].as<std::uint64_t>(c.nic.iops_read_small);
    c.nic.iops_write_small = n["iops_write_small"].as<std::uint64_t>(c.nic.iops_write_small);
    c.nic.in_order_rc = n["in_order_rc"].as<bool>(c.nic.in_order_rc);
    c.nic.qp_per_thread = n["qp_per_thread"].as<int>(c.nic.qp_per_thread);

    // Advanced
    c.nic.tb_cas_ops_per_s = n["tb_cas_ops_per_s"].as<double>(c.nic.tb_cas_ops_per_s);
    c.nic.tb_read_ops_per_s = n["tb_read_ops_per_s"].as<double>(c.nic.tb_read_ops_per_s);
    c.nic.tb_write_ops_per_s = n["tb_write_ops_per_s"].as<double>(c.nic.tb_write_ops_per_s);
    c.nic.tb_burst_ops = n["tb_burst_ops"].as<double>(c.nic.tb_burst_ops);
    c.nic.small_threshold = n["small_threshold"].as<std::size_t>(c.nic.small_threshold);

    c.nic.pcie_doorbell_us = n["pcie_doorbell_us"].as<double>(c.nic.pcie_doorbell_us);
    c.nic.pcie_desc_us = n["pcie_desc_us"].as<double>(c.nic.pcie_desc_us);
    c.nic.doorbell_batch_limit = n["doorbell_batch_limit"].as<int>(c.nic.doorbell_batch_limit);
    c.nic.sq_depth = n["sq_depth"].as<int>(c.nic.sq_depth);
  }

  // sherman
  if (auto sh = y["sherman"]; sh){
    c.index.sh.combine = sh["combine_commands"].as<bool>(c.index.sh.combine);
    if (auto hocl = sh["hocl"]) {
      c.index.sh.hocl.enable    = hocl["enable"].as<bool>(c.index.sh.hocl.enable);
      c.index.sh.hocl.glt_slots = hocl["glt_slots"].as<int>(c.index.sh.hocl.glt_slots);
      c.index.sh.hocl.llt_enable= hocl["llt_enable"].as<bool>(c.index.sh.hocl.llt_enable);
      c.index.sh.hocl.llt_local_wait_us = hocl["llt_local_wait_us"].as<double>(c.index.sh.hocl.llt_local_wait_us);
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
  }

  // workloads
  if (auto wls = y["workloads"]; wls && wls.IsSequence()) {
    c.workloads.clear();
    for (const auto& wl : wls) {
      WorkloadCfg cfg;
      cfg.name = wl["name"].as<std::string>("unnamed");
      cfg.ops = wl["ops"].as<std::size_t>(1000);
      if (auto mix = wl["mix"]) {
        cfg.mix.read = mix["read"].as<double>(1.0);
        cfg.mix.write = mix["write"].as<double>(0.0);
      }
      cfg.keyspace = wl["keyspace"].as<std::uint64_t>(100000);
      cfg.zipf = wl["zipf"].as<double>(0.99);
      cfg.range_len = wl["range_len"].as<std::uint32_t>(1);
      c.workloads.push_back(cfg);
    }
  }

  // metrics
  if (auto metrics = y["metrics"]; metrics) {
    c.metrics.out_dir = metrics["out_dir"].as<std::string>(c.metrics.out_dir);
    c.metrics.dump_per_op_trace = metrics["dump_per_op_trace"].as<bool>(c.metrics.dump_per_op_trace);
    if (auto ptiles = metrics["ptiles"]; ptiles && ptiles.IsSequence()) {
      c.metrics.ptiles.clear();
      for (const auto& p : ptiles) {
        c.metrics.ptiles.push_back(p.as<int>());
      }
    }
  }

  return c;
}