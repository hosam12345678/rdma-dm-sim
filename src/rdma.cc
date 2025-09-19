#include "sim/rdma.h"

NIC::NIC(EventLoop& l, const Caps& in_caps) : loop(l), caps(in_caps){
  nic_tb_read.init(1e12, 1024, loop.now);
  nic_tb_write.init(1e12, 1024, loop.now);
  nic_tb_cas.init(1e12, 1024, loop.now);
}

double NIC::bytes_per_us() const { return (caps.link_gbps * 1e3) / 8.0; }

static TokenBucket& pick_bucket(QPState& st, const RdmaReq& r){
  if (r.verb==Verb::CAS)   return st.tb_cas;
  if (r.verb==Verb::READ)  return st.tb_read;
  return st.tb_write; // WRITE/SEND/RECV
}

Completion NIC::post(const RdmaReq& r){
  long long key = (static_cast<long long>(r.cs_id) << 32) | r.qp;
  auto& st = qpstate[key];
  // lazy-init buckets with caps
  if (st.tb_read.rate_ops_per_us==0){
    st.tb_read.init(caps.tb_read_ops_per_s, caps.tb_burst_ops, loop.now);
    st.tb_write.init(caps.tb_write_ops_per_s, caps.tb_burst_ops, loop.now);
    st.tb_cas.init(caps.tb_cas_ops_per_s, caps.tb_burst_ops, loop.now);
  }

  // 1) host posting costs (descriptor + doorbell; for single WQE, pay both)
  double t = std::max(loop.now, st.post_ready_at);
  t = st.post_ready_at = t + caps.pcie_desc_us + caps.pcie_doorbell_us;

  // 2) SQ depth: if full, wait until completion frontier
  if (st.outstanding >= caps.sq_depth){
    st.post_ready_at = std::max(st.post_ready_at, st.ready_at);
    st.outstanding = std::max(0, st.outstanding - 1);
  }

  // 3) token buckets per QP
  auto& tb = pick_bucket(st, r);
  double t_tokens = tb.acquire(1.0, st.post_ready_at);

  // 4) wire/NIC service
  SimTime svc = 0.0;
  if (r.verb == Verb::CAS && r.tgt == Target::RNIC_ONCHIP) {
    svc = caps.cas_onchip_rtt_us;
  } else {
    SimTime base = caps.base_rtt_us;
    SimTime size_term = static_cast<double>(r.bytes) / bytes_per_us();
    svc = base + size_term;
  }

  // 5) completion frontier (in-order per QP)
  SimTime start = std::max({loop.now, st.ready_at, t_tokens});
  SimTime done  = start + svc;
  st.ready_at = done; st.outstanding++;
  loop.at(done, [&st]{ st.outstanding = std::max(0, st.outstanding - 1); });
  return Completion{done};
}

Completion NIC::post_chain(const std::vector<RdmaReq>& chain){
  if (chain.empty()) return Completion{loop.now};
  // amortize doorbells: pay descriptors for all, doorbells per batch
  long long key = (static_cast<long long>(chain.front().cs_id) << 32) | chain.front().qp;
  auto& st = qpstate[key];
  double t = std::max(loop.now, st.post_ready_at);
  int n = (int)chain.size();
  int batches = (n + caps.doorbell_batch_limit - 1) / caps.doorbell_batch_limit;
  t += n * caps.pcie_desc_us + batches * caps.pcie_doorbell_us;
  st.post_ready_at = t;
  Completion c{loop.now};
  for (auto& r : chain) c = post(r);
  return c;
}
