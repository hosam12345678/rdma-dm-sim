#pragma once
#include "sim/event_loop.h"
#include "sim/rdma.h"
#include "sim/cache.h"
#include "sim/metrics.h"
#include <memory>
#include <vector>

struct IndexCtx { EventLoop* loop{nullptr}; NIC* nic{nullptr}; int cs_id{0}, ms_id{0}; int qp{0}; std::size_t node_bytes{4096}, leaf_entry_bytes{24}; };

struct Index {
  IndexCtx ctx;
  virtual ~Index() = default;
  virtual void get(std::uint64_t key, Metrics& m, std::uint64_t op_id) = 0;
  virtual void put(std::uint64_t key, Metrics& m, std::uint64_t op_id) = 0;
};
