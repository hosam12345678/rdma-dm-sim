#include "sim/index_sherman.h"
#include "sim/config.h"
#include <algorithm>
#include <cstdlib>

Sherman::Sherman(const IndexCtx& c, ShermanConf sc, std::size_t cache_bytes)
  : conf(sc), glt(sc.hocl.glt_slots), cache(cache_bytes) { 
  ctx=c; 
  
  // Configure RDWC delegation table
  delegation_table.config.enable = sc.rdwc.enable;
  delegation_table.config.window_ns = std::chrono::microseconds(static_cast<long>(sc.rdwc.window_us));
  delegation_table.config.collision_policy = 
    (sc.rdwc.collision_policy == ShermanConf::rdwc_t::CollisionPolicy::BYPASS) ? 
    rdwc::DelegationTable::Config::BYPASS : rdwc::DelegationTable::Config::QUEUE;
}

std::uint64_t Sherman::path_to_leaf(std::uint64_t key, std::vector<std::uint64_t>& nodes){
  std::uint64_t n1 = key >> 32, n2 = key >> 16, leaf = key;
  nodes = {n1, n2, leaf}; return leaf;
}

void Sherman::read_node(std::uint64_t node_id, int level, Metrics& m, SimTime& completion){
  bool hit = cache.get({node_id, level});
  if (hit) return;
  RdmaReq r{Verb::READ, Target::DRAM, ctx.node_bytes, ctx.qp, ctx.cs_id, ctx.ms_id};
  auto c = ctx.nic->post(r);
  completion = std::max(completion, c.when);
  m.remote_reads++; m.bytes_read += ctx.node_bytes;
  cache.put({node_id, level}, ctx.node_bytes);
}

int Sherman::leaf_capacity() const { return conf.leaf_max_entries>0 ? conf.leaf_max_entries : (int)(ctx.node_bytes / ctx.leaf_entry_bytes); }

std::uint64_t Sherman::glt_slot(std::uint64_t leaf) const {
  if (!conf.model_glt_collisions) return leaf % glt.slots;
  std::uint64_t x = leaf ^ (std::uint64_t)conf.hocl.glt_slots ^ (std::uint64_t)conf.glt_hash_seed;
  x ^= (x >> 33); x *= 0xff51afd7ed558ccdull; x ^= (x >> 33); x *= 0xc4ceb9fe1a85ec53ull; x ^= (x >> 33);
  return x % glt.slots;
}

// Always acquire a real lock.
// - HOCL enabled: LLT (local fairness queue) -> GLT (on-chip CAS spin)
// - HOCL disabled: DRAM CAS spin (no LLT), higher RTT via NIC model
void Sherman::hocl_acquire(std::uint64_t leaf, int tid, Metrics& m, SimTime& completion){
  const bool hocl = conf.hocl.enable;
  const auto slot = glt_slot(leaf);

  // LLT: enqueue and model local queueing delay (no NIC), to hand over fairly.
  if (hocl && conf.hocl.llt_enable){
    int pos = llt.enqueue_and_pos(leaf, tid);
    if (pos > 0){
      // completion += conf.hocl.hocl.llt_local_wait_us; // per-position unit delay (can be tuned)
      completion += conf.hocl.llt_local_wait_us * pos;
    }
  }

  // GLT/DRAM lock via CAS spin, NIC-timed
  Target cas_target = hocl ? Target::RNIC_ONCHIP : Target::DRAM;

  int retries = 0;
  while (true){
    RdmaReq cas{Verb::CAS, cas_target, 8, ctx.qp, ctx.cs_id, ctx.ms_id};
    auto c = ctx.nic->post(cas);
    completion = std::max(completion, c.when);
    m.remote_cas++;

    // If lock is free and we're allowed (head when LLT enabled), take it.
    const bool at_head = (!hocl || !conf.hocl.llt_enable) ? true : llt.at_head(leaf, tid);
    if (at_head && glt.owner[slot] == -1){
      glt.owner[slot] = tid;
      break;
    }
    // Else backoff and retry
    retries++;
    completion += conf.cas_backoff_us;
    if (retries >= conf.cas_max_retries){
      // Break after extended retries to avoid infinite loop in simulation
      // Force acquire the lock to ensure progress
      glt.owner[slot] = tid;
      break;
    }
  }
}

// Schedule-only release (used when unlock RDMA is already part of a chain)
void Sherman::hocl_release_state_at(std::uint64_t leaf, int tid, SimTime when){
  auto slot = glt_slot(leaf);
  ctx.loop->at(when, [this, leaf, tid, slot]{
    // Free GLT owner
    if (slot < (std::size_t)glt.owner.size() && glt.owner[slot] == tid){
      glt.owner[slot] = -1;
    } else {
      // If owner mismatch, clear anyway to avoid permanent wedging in a sim
      if (slot < (std::size_t)glt.owner.size()) glt.owner[slot] = -1;
    }
    // Release LLT head
    if (conf.hocl.enable && conf.hocl.llt_enable) llt.release(leaf, tid);
  });
}

// Post unlock (writes lock word) and schedule state release when NIC completes.
void Sherman::hocl_release(std::uint64_t leaf, int tid, Metrics& m, SimTime& completion){
  Target unlock_target = conf.hocl.enable ? Target::RNIC_ONCHIP : Target::DRAM;
  RdmaReq w{Verb::WRITE, unlock_target, 8, ctx.qp, ctx.cs_id, ctx.ms_id};
  auto c = ctx.nic->post(w);
  completion = std::max(completion, c.when);
  m.remote_writes++; m.bytes_write += 8;

  hocl_release_state_at(leaf, tid, c.when);
}

void Sherman::get(std::uint64_t key, Metrics& m, std::uint64_t op_id){
  if (conf.rdwc.enable) {
    // Try RDWC delegation
    auto [is_delegate, entry] = delegation_table.try_delegate_get(key, op_id, 
      [&](bool success, const std::string& result) {
        // Waiter callback - operation completed by delegate
        if (success) {
          m.ops++;
          // For simulation, we don't track the actual result content
          // The delegate already updated the metrics for RDMA operations
        }
      });
    
    if (!is_delegate) {
      // This thread is a waiter, delegate will handle the operation
      return;
    }
    
    // This thread is the delegate, execute the operation
    delegate_get_impl(key, m, op_id);
    
    // Notify waiters
    std::uint64_t key_hash = std::hash<std::uint64_t>{}(key);
    delegation_table.complete_delegation(key_hash, true, "success");
    return;
  }
  
  // Original GET implementation (non-delegated)
  delegate_get_impl(key, m, op_id);
}

void Sherman::delegate_get_impl(std::uint64_t key, Metrics& m, std::uint64_t op_id){
  SimTime start = ctx.loop->now, done = start; std::uint64_t br0=m.bytes_read, bw0=m.bytes_write; auto rr0=m.remote_reads.load(), rw0=m.remote_writes.load(), rc0=m.remote_cas.load();
  std::vector<std::uint64_t> nodes; auto leaf = path_to_leaf(key, nodes);
  for (int lvl=0; lvl<(int)nodes.size(); ++lvl) read_node(nodes[lvl], lvl, m, done);
  RdmaReq r{Verb::READ, Target::DRAM, ctx.leaf_entry_bytes, ctx.qp, ctx.cs_id, ctx.ms_id};
  auto c = ctx.nic->post(r); done = std::max(done, c.when); m.remote_reads++; m.bytes_read += ctx.leaf_entry_bytes;

  // Version validation (node-level then entry-level)
  auto& meta = leafs[leaf]; if (meta.entry_ver.empty()) meta.entry_ver.resize(leaf_capacity(), 0);
  std::uint64_t node_ver_before = meta.node_ver;
  bool retry=false;
  if (conf.enable_two_level_versions){
    if (meta.node_ver != node_ver_before) retry=true;
    int idx = (int)(key % leaf_capacity());
    std::uint64_t ev_before = meta.entry_ver[idx];
    (void)ev_before;
    if (meta.entry_ver[idx] != ev_before) retry=true;
  }
  if (retry){
    RdmaReq r2{Verb::READ, Target::DRAM, ctx.node_bytes, ctx.qp, ctx.cs_id, ctx.ms_id};
    auto c2 = ctx.nic->post(r2); done = std::max(done, c2.when); m.remote_reads++; m.bytes_read += ctx.node_bytes;
  }

  ctx.loop->at(done, [&, start, done, op_id, rr0, rw0, rc0, br0, bw0]{ m.ops++; double lat=done-start; m.add_latency(lat); m.dump_op(op_id, "GET", lat, m.remote_reads-rr0, m.remote_writes-rw0, m.remote_cas-rc0, 0, 0, m.bytes_read-br0, m.bytes_write-bw0); });
}

void Sherman::put(std::uint64_t key, Metrics& m, std::uint64_t op_id){
  if (conf.rdwc.enable) {
    // Try RDWC delegation for writes
    auto [is_delegate, entry] = delegation_table.try_delegate_put(key, op_id,
      [this, key, &m, op_id]() {
        // This lambda will be executed by the delegate
        delegate_put_impl(key, m, op_id);
      });
    
    if (!is_delegate) {
      // This thread's write was combined with the delegate's
      return;
    }
    
    // This thread is the delegate - execute combined writes
    if (entry && !entry->pending_writes.empty()) {
      for (auto& write_op : entry->pending_writes) {
        write_op(); // Execute each combined write
      }
    } else {
      // Single write case
      delegate_put_impl(key, m, op_id);
    }
    
    // Complete delegation
    std::uint64_t key_hash = std::hash<std::uint64_t>{}(key);
    delegation_table.complete_delegation(key_hash, true, "success");
    return;
  }
  
  // Original PUT implementation (non-delegated)
  delegate_put_impl(key, m, op_id);
}

void Sherman::delegate_put_impl(std::uint64_t key, Metrics& m, std::uint64_t op_id){
  SimTime start = ctx.loop->now, done = start; int tid = 0; std::uint64_t br0=m.bytes_read, bw0=m.bytes_write; auto rr0=m.remote_reads.load(), rw0=m.remote_writes.load(), rc0=m.remote_cas.load();
  std::vector<std::uint64_t> nodes; auto leaf = path_to_leaf(key, nodes);
  for (int lvl=0; lvl<(int)nodes.size(); ++lvl) read_node(nodes[lvl], lvl, m, done);

  // Always acquire a lock (HOCL -> GLT on-chip; disabled -> DRAM)
  hocl_acquire(leaf, tid, m, done);

  if (conf.combine){
    // Combine write-back + unlock on the same QP (paper’s optimization)
    Target unlock_target = conf.hocl.enable ? Target::RNIC_ONCHIP : Target::DRAM;
    std::vector<RdmaReq> chain = {
      RdmaReq{Verb::WRITE, Target::DRAM,        ctx.leaf_entry_bytes, ctx.qp, ctx.cs_id, ctx.ms_id},
      RdmaReq{Verb::WRITE, unlock_target,       8,                    ctx.qp, ctx.cs_id, ctx.ms_id}
    };
    auto c = ctx.nic->post_chain(chain); done = std::max(done, c.when);
    m.remote_writes += 2; m.bytes_write += (ctx.leaf_entry_bytes + 8);

    // Schedule state release exactly when the chain completes
    hocl_release_state_at(leaf, tid, c.when);
  } else {
    RdmaReq w{Verb::WRITE, Target::DRAM, ctx.leaf_entry_bytes, ctx.qp, ctx.cs_id, ctx.ms_id};
    auto c1 = ctx.nic->post(w); done = std::max(done, c1.when); m.remote_writes++; m.bytes_write += ctx.leaf_entry_bytes;
    // Post unlock and schedule release
    hocl_release(leaf, tid, m, done);
  }

  // Update leaf meta (versions, occupancy, splits)
  auto& meta = leafs[leaf]; if (meta.entry_ver.empty()) meta.entry_ver.resize(leaf_capacity(), 0);
  int idx = (int)(key % leaf_capacity());
  if (conf.enable_two_level_versions) { meta.entry_ver[idx]++; }
  meta.node_ver++;
  meta.entries = std::min(leaf_capacity(), meta.entries + 1);

  // Split if above threshold
  if (conf.enable_splits && meta.entries >= (int)(conf.split_threshold * leaf_capacity())){
    std::uint64_t sib = leaf ^ 0x5bd1e995u;
    auto& sm = leafs[sib]; if (sm.entry_ver.empty()) sm.entry_ver.resize(leaf_capacity(), 0);
    int moved = meta.entries / 2; meta.entries -= moved; sm.entries += moved; meta.node_ver++; sm.node_ver++;

    if (conf.enable_two_level_versions){
      RdmaReq wsib{Verb::WRITE, Target::DRAM, ctx.node_bytes, ctx.qp, ctx.cs_id, ctx.ms_id};
      auto cws = ctx.nic->post(wsib); done = std::max(done, cws.when); m.remote_writes++; m.bytes_write += ctx.node_bytes;
      RdmaReq wpar{Verb::WRITE, Target::DRAM, 64, ctx.qp, ctx.cs_id, ctx.ms_id};
      auto cwp = ctx.nic->post(wpar); done = std::max(done, cwp.when); m.remote_writes++; m.bytes_write += 64;
    }
  }

  ctx.loop->at(done, [&, start, done, op_id, rr0, rw0, rc0, br0, bw0]{ m.ops++; double lat=done-start; m.add_latency(lat); m.dump_op(op_id, "PUT", lat, m.remote_reads-rr0, m.remote_writes-rw0, m.remote_cas-rc0, 0, 0, m.bytes_read-br0, m.bytes_write-bw0); });
}