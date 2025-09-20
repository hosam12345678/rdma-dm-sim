#pragma once
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>
#include <algorithm>

struct GLT { // global lock table (conceptual)
  int slots{0};
  // owner per slot: -1 = free, otherwise tid (or any small owner id)
  std::vector<int> owner;
  explicit GLT(int n): slots(n), owner(n, -1) {}
};

struct LLT { // local fairness & handoff (per-CS)
  // per-leaf local FIFO of threads
  std::unordered_map<std::uint64_t, std::deque<int>> waiters;

  // Enqueue tid if not present; return its position in FIFO
  int enqueue_and_pos(std::uint64_t key, int tid){
    auto& q = waiters[key];
    auto it = std::find(q.begin(), q.end(), tid);
    if (it == q.end()) {
      q.push_back(tid);
      return static_cast<int>(q.size()) - 1;
    }
    return static_cast<int>(std::distance(q.begin(), it));
  }

  // Is tid at head?
  bool at_head(std::uint64_t key, int tid){
    auto& q = waiters[key];
    return !q.empty() && q.front() == tid;
  }

  // Release head if tid owns it
  void release(std::uint64_t key, int tid){
    auto& q = waiters[key];
    if (!q.empty() && q.front() == tid) {
      q.pop_front();
    } else {
      // best-effort cleanup if tid is somewhere else (shouldn't happen on correct usage)
      auto it = std::find(q.begin(), q.end(), tid);
      if (it != q.end()) q.erase(it);
    }
    if (q.empty()){
      // optional: shrink map usability
      // waiters.erase(key); // keep for locality if you prefer
    }
  }
};