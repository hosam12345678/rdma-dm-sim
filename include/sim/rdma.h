#pragma once
#include "sim/types.h"
#include "sim/event_loop.h"
#include <unordered_map>
#include <algorithm>

struct TokenBucket {
  double rate_ops_per_us{0};
  double burst{0};
  double tokens{0};
  double last_refill{0};
  void init(double ops_per_s, double burst_, double now){ rate_ops_per_us=ops_per_s/1e6; burst=burst_; tokens=burst; last_refill=now; }
  double acquire(double need, double now){
    // Refill
    tokens = std::min(burst, tokens + (now - last_refill)*rate_ops_per_us);
    last_refill = now;
    if (tokens >= need){ tokens -= need; return now; }
    double deficit = need - tokens;
    double wait_us = deficit / rate_ops_per_us;
    tokens = 0;
    last_refill = now + wait_us;
    return now + wait_us;
  }
};

struct QPState {
  SimTime ready_at{0.0};      // completion frontier
  SimTime post_ready_at{0.0}; // PCIe posting frontier
  int outstanding{0};
  TokenBucket tb_cas, tb_read, tb_write;
};

struct NIC {
  EventLoop& loop;
  struct Caps {
    double link_gbps, base_rtt_us, per_byte_us, cas_onchip_rtt_us;
    bool in_order_rc; int qp_per_thread;
    std::size_t small_threshold; int doorbell_batch_limit;
    double pcie_doorbell_us, pcie_desc_us; int sq_depth;
    double tb_cas_ops_per_s, tb_read_ops_per_s, tb_write_ops_per_s, tb_burst_ops;
  } caps;
  std::unordered_map<long long, QPState> qpstate; // key = ((long long)cs<<32)|qp
  TokenBucket nic_tb_read, nic_tb_write, nic_tb_cas;

  NIC(EventLoop& l, const Caps& in_caps);
  double bytes_per_us() const;
  Completion post(const RdmaReq& r);
  Completion post_chain(const std::vector<RdmaReq>& chain);
};
