#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cmath>

struct Hist {
  void clear() { vals.clear(); }
  std::vector<double> vals;
  void add(double v){ vals.push_back(v); }
  double pct(double p) const {
    if (vals.empty()) return 0.0;
    auto v = vals; std::sort(v.begin(), v.end());
    std::size_t idx = std::min<std::size_t>(static_cast<std::size_t>(std::floor((p/100.0)*(v.size()-1))), v.size()-1);
    return v[idx];
  }
};

struct Metrics {
  void reset() {
    ops = 0;
    remote_reads = 0;
    remote_writes = 0;
    remote_cas = 0;
    send_ops = 0;
    recv_ops = 0;
    bytes_read = 0;
    bytes_write = 0;
    hopscotch_hits = 0;
    lat_us.clear();
    trace_enabled = false;
    if (trace.is_open()) trace.close();
  }
  std::atomic<std::uint64_t> ops{0};
  std::atomic<std::uint64_t> remote_reads{0}, remote_writes{0}, remote_cas{0}, send_ops{0}, recv_ops{0};
  std::atomic<std::uint64_t> bytes_read{0}, bytes_write{0};
  std::atomic<std::uint64_t> hopscotch_hits{0}; // Sprint 2: hopscotch overlay hits
  std::mutex lat_m; Hist lat_us;

  // Optional per-op CSV trace
  bool trace_enabled{false};
  std::ofstream trace;
  void open_trace(const std::string& path){ if(trace_enabled){ trace.open(path); trace << "op_id,type,latency_us,reads,writes,cas,sends,recvs,bytes_r,bytes_w\n"; } }
  void add_latency(double us){ std::lock_guard<std::mutex> g(lat_m); lat_us.add(us); }
  void dump_op(std::uint64_t id, const std::string& type, double lat, std::uint64_t r, std::uint64_t w, std::uint64_t c, std::uint64_t s, std::uint64_t rv, std::uint64_t br, std::uint64_t bw){
    if (!trace_enabled || !trace.good()) return;
    trace << id << ',' << type << ',' << lat << ',' << r << ',' << w << ',' << c << ',' << s << ',' << rv << ',' << br << ',' << bw << "\n";
  }
};
