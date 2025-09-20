#pragma once
#include "sim/index.h"
#include "sim/locks.h"
#include "sim/cache.h"
#include "sim/metrics.h"
#include "sim/config.h"
#include "sim/rdwc.h"
#include "sim/hopscotch.h"
#include <unordered_map>

struct Sherman : public Index {
  ShermanConf conf;      // store by value (allows ablated copy)
  GLT glt;
  LLT llt;
  LRUCache cache;
  rdwc::DelegationTable delegation_table; // RDWC delegation

  // Leaf occupancy & versions
  struct LeafMeta { 
    int entries{0}; 
    std::uint64_t node_ver{0}; 
    std::vector<std::uint64_t> entry_ver; 
    // Hopscotch overlay for accelerated lookups
    std::unique_ptr<hopscotch::HopscotchOverlay> overlay{nullptr};
    std::uint64_t access_count{0}; // for topK tracking
  };
  std::unordered_map<std::uint64_t, LeafMeta> leafs; // leaf_id -> meta

  Sherman(const IndexCtx& c, ShermanConf sc, std::size_t cache_bytes);
  void get(std::uint64_t key, Metrics& m, std::uint64_t op_id) override;
  void put(std::uint64_t key, Metrics& m, std::uint64_t op_id) override;
private:
  std::uint64_t path_to_leaf(std::uint64_t key, std::vector<std::uint64_t>& nodes);
  void read_node(std::uint64_t node_id, int level, Metrics& m, SimTime& completion);
  void hocl_acquire(std::uint64_t leaf, int tid, Metrics& m, SimTime& completion);
  void hocl_release(std::uint64_t leaf, int tid, Metrics& m, SimTime& completion);
  void hocl_release_state_at(std::uint64_t leaf, int tid, SimTime when);

  // RDWC internal methods
  void delegate_get_impl(std::uint64_t key, Metrics& m, std::uint64_t op_id);
  void delegate_put_impl(std::uint64_t key, Metrics& m, std::uint64_t op_id);

  // Hopscotch overlay methods
  void hopscotch_maybe_create_overlay(std::uint64_t leaf_id, Metrics& m);
  void hopscotch_update_overlay(std::uint64_t leaf_id, std::uint64_t key, int leaf_slot);
  void hopscotch_remove_from_overlay(std::uint64_t leaf_id, std::uint64_t key);
  int hopscotch_probe_overlay(std::uint64_t leaf_id, std::uint64_t key, Metrics& m);
  void hopscotch_track_leaf_access(std::uint64_t leaf_id);

  int leaf_capacity() const;
  std::uint64_t glt_slot(std::uint64_t leaf) const;
};