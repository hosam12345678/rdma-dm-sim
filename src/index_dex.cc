#include "sim/index_dex.h"
#include "sim/config.h"
#include <algorithm>
#include <cstdlib>

void Dex::init_partitions(){
  bucket_owner.assign(conf.num_partitions, 0);
  for (int i=0;i<conf.num_partitions;i++) bucket_owner[i] = i % cs_total;
}

int Dex::bucket_of(std::uint64_t key) const { return (int)((key % conf.num_partitions)); }

void Dex::schedule_repartition(){
  ctx.loop->after(conf.repartition_period_ms*1000.0, [this]{ this->do_repartition(); this->schedule_repartition(); });
}

void Dex::do_repartition(){
  // Broadcast map update cost (book-keep as SEND)
  RdmaReq s{Verb::SEND, Target::DRAM, 128, ctx.qp, ctx.cs_id, ctx.ms_id};
  ctx.nic->post(s);
  // Invalidate a fraction of cache
  if ((rand()%100) < (int)(conf.cache_inval_prob*100)) { cache = LRUCache(cache.cap_bytes); }
  // Rotate top-K buckets among owners
  for (int k=0;k<conf.repartition_topK && k<conf.num_partitions; ++k){
    int b = k;
    bucket_owner[b] = (bucket_owner[b] + 1) % cs_total;
  }
}

Dex::Dex(const IndexCtx& c, DexConf dx, std::size_t cache_bytes, int cs_total_, double ms_budget)
  : conf(dx), cache(cache_bytes), cs_total(cs_total_) { ctx=c; msq.budget_ops_per_s=ms_budget; init_partitions(); schedule_repartition(); }

int Dex::owner_cs(std::uint64_t key) const { return static_cast<int>((key>>56) % cs_total); }

SimTime Dex::offload_cost_est(int range_len) const {
  double svc = (static_cast<double>(range_len) / msq.budget_ops_per_s) * 1e6; // us
  double rtt = 4.0; // rough SEND/RECV overhead
  return svc + rtt;
}

SimTime Dex::onesided_cost_est(int misses, std::size_t bytes) const {
  return misses * (ctx.nic->caps.base_rtt_us + static_cast<double>(bytes)/ctx.nic->bytes_per_us());
}

void Dex::get(std::uint64_t key, Metrics& m, std::uint64_t op_id){
  SimTime start = ctx.loop->now, done = start; std::uint64_t br0=m.bytes_read, bw0=m.bytes_write; auto rr0=m.remote_reads.load(), rw0=m.remote_writes.load(), rc0=m.remote_cas.load();
  (void)rc0; // not used here

  int bucket = bucket_of(key);
  int owner = conf.logical_partitioning ? bucket_owner[bucket] : ctx.cs_id;
  bool local_owner = (owner==ctx.cs_id);
  if (!local_owner){
    RdmaReq s1{Verb::SEND, Target::DRAM, 64, ctx.qp, ctx.cs_id, ctx.ms_id};
    auto cs1 = ctx.nic->post(s1); m.send_ops++;
    RdmaReq r1{Verb::RECV, Target::DRAM, 64, ctx.qp, ctx.cs_id, ctx.ms_id};
    auto cr1 = ctx.nic->post(r1); m.recv_ops++;
    done = std::max({done, cs1.when, cr1.when});
  }

  // Path-aware cache: up to two internal node touches
  for (int lvl=0; lvl<2; ++lvl){
    CacheKey k{ (key>>(16*(2-lvl))), lvl };
    bool hit = conf.path_aware_cache && cache.get(k);
    if(!hit){
      RdmaReq r{Verb::READ, Target::DRAM, ctx.node_bytes, ctx.qp, ctx.cs_id, ctx.ms_id};
      auto c = ctx.nic->post(r); done=std::max(done,c.when); m.remote_reads++; m.bytes_read += ctx.node_bytes;
      if (conf.path_aware_cache) cache.put(k, ctx.node_bytes);
    }
  }
  bool use_offload=false;
  if (conf.offload.enable){
    SimTime est1 = onesided_cost_est(1, ctx.leaf_entry_bytes);
    SimTime est2 = offload_cost_est(1);
    use_offload = est2 < est1;
  }
  if(use_offload){
    SimTime start_ms = std::max(done, msq.ready_at);
    double svc_us = (1.0 / msq.budget_ops_per_s) * 1e6; SimTime fin_ms = start_ms + svc_us; msq.ready_at = fin_ms;
    RdmaReq s{Verb::SEND, Target::DRAM, 64, ctx.qp, ctx.cs_id, ctx.ms_id};
    RdmaReq r{Verb::RECV, Target::DRAM, 64, ctx.qp, ctx.cs_id, ctx.ms_id};
    auto c1=ctx.nic->post(s), c2=ctx.nic->post(r);
    done = std::max({done, fin_ms, c1.when, c2.when}); m.send_ops++; m.recv_ops++;
  } else {
    RdmaReq r{Verb::READ, Target::DRAM, ctx.leaf_entry_bytes, ctx.qp, ctx.cs_id, ctx.ms_id};
    auto c = ctx.nic->post(r); done=std::max(done,c.when); m.remote_reads++; m.bytes_read += ctx.leaf_entry_bytes;
  }
  ctx.loop->at(done, [&, start, done, op_id, rr0, rw0, br0, bw0]{ m.ops++; double lat=done-start; m.add_latency(lat); m.dump_op(op_id, "GET", lat, m.remote_reads-rr0, m.remote_writes-rw0, 0, m.send_ops, m.recv_ops, m.bytes_read-br0, m.bytes_write-bw0); });
}

void Dex::put(std::uint64_t key, Metrics& m, std::uint64_t op_id){
  // Reuse get path, then do a small write
  get(key, m, op_id);
  SimTime done = std::max(ctx.loop->now, 0.0); std::uint64_t bw0=m.bytes_write; auto rw0=m.remote_writes.load();
  RdmaReq w{Verb::WRITE, Target::DRAM, ctx.leaf_entry_bytes, ctx.qp, ctx.cs_id, ctx.ms_id};
  auto c = ctx.nic->post(w); done=std::max(done,c.when); m.remote_writes++; m.bytes_write += ctx.leaf_entry_bytes;
  ctx.loop->at(done, [&, s=ctx.loop->now, done, op_id, rw0, bw0]{ m.ops++; double lat=done-s; m.add_latency(lat); m.dump_op(op_id, "PUT", lat, 0, m.remote_writes-rw0, 0, 0, 0, 0, m.bytes_write-bw0); });
}
