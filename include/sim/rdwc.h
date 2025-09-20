#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>

namespace rdwc {

enum class DelegationState { ACTIVE, COMPLETED, FAILED };

struct Waiter {
  std::uint64_t op_id;
  std::function<void(bool success, const std::string& result)> callback;
};

struct DelegationEntry {
  std::uint64_t unique_key;
  DelegationState state{DelegationState::ACTIVE};
  std::vector<Waiter> waiters;
  bool write_pending{false};
  std::vector<std::function<void()>> pending_writes; // for write combining
  std::chrono::steady_clock::time_point start_time;
  std::mutex mtx;
  std::condition_variable cv;
  std::string result; // cached result for GET delegation
};

class DelegationTable {
private:
  static constexpr size_t NUM_SHARDS = 64;
  
  struct Shard {
    std::mutex mtx;
    std::unordered_map<std::uint64_t, std::shared_ptr<DelegationEntry>> entries;
  };
  
  std::vector<Shard> shards;
  
  size_t shard_for_key(std::uint64_t key_hash) const {
    return key_hash % NUM_SHARDS;
  }

public:
  // Config knobs
  struct Config {
    bool enable{true};
    std::chrono::nanoseconds window_ns{std::chrono::microseconds(100)}; // 100us window
    enum Policy { BYPASS, QUEUE } collision_policy{QUEUE};
  } config;

  DelegationTable() : shards(NUM_SHARDS) {}

  // Returns: {is_delegate, delegation_entry}
  // If is_delegate=true, caller should execute the operation and notify waiters
  // If is_delegate=false, caller should wait on delegation_entry or bypass
  std::pair<bool, std::shared_ptr<DelegationEntry>> 
  try_delegate_get(std::uint64_t key, std::uint64_t op_id, 
                   std::function<void(bool, const std::string&)> callback);

  std::pair<bool, std::shared_ptr<DelegationEntry>>
  try_delegate_put(std::uint64_t key, std::uint64_t op_id,
                   std::function<void()> write_op);

  void complete_delegation(std::uint64_t key_hash, bool success, const std::string& result = "");
  
  void cleanup_expired();
  
  // Metrics
  struct Stats {
    std::atomic<std::uint64_t> delegations_created{0};
    std::atomic<std::uint64_t> delegation_hits{0};
    std::atomic<std::uint64_t> delegation_bypasses{0};
    std::atomic<std::uint64_t> write_combines{0};
  } stats;
};

} // namespace rdwc