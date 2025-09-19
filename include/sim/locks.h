#pragma once
#include <cstdint>
#include <deque>
#include <unordered_map>

struct GLT { // global lock table (conceptual)
  int slots{0};
  explicit GLT(int n): slots(n) {}
};

struct LLT { // local fairness & handoff
  std::unordered_map<std::uint64_t, std::deque<int>> waiters;
  bool try_acquire(std::uint64_t key, int tid){ auto& q=waiters[key]; if(q.empty() || q.front()==tid){ if(q.empty()) q.push_back(tid); return true; } q.push_back(tid); return false; }
  void release(std::uint64_t key, int tid){ auto& q=waiters[key]; if(!q.empty() && q.front()==tid){ q.pop_front(); } }
};
