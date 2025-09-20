#include "sim/rdwc.h"
#include <algorithm>

namespace rdwc {

std::pair<bool, std::shared_ptr<DelegationEntry>> 
DelegationTable::try_delegate_get(std::uint64_t key, std::uint64_t op_id,
                                  std::function<void(bool, const std::string&)> callback) {
  if (!config.enable) {
    return {true, nullptr}; // Bypass delegation, caller becomes delegate
  }

  std::uint64_t key_hash = std::hash<std::uint64_t>{}(key);
  auto& shard = shards[shard_for_key(key_hash)];
  
  std::lock_guard<std::mutex> lock(shard.mtx);
  
  auto it = shard.entries.find(key_hash);
  if (it == shard.entries.end()) {
    // First thread for this key - becomes delegate
    auto entry = std::make_shared<DelegationEntry>();
    entry->unique_key = key;
    entry->start_time = std::chrono::steady_clock::now();
    shard.entries[key_hash] = entry;
    stats.delegations_created++;
    return {true, entry};
  }
  
  auto entry = it->second;
  
  // Check if this is actually the same key (handle hash collisions)
  if (entry->unique_key != key) {
    if (config.collision_policy == Config::BYPASS) {
      stats.delegation_bypasses++;
      return {true, nullptr}; // Bypass on collision
    }
    // For QUEUE policy, still join the delegation (conservative)
  }
  
  // Check if delegation window has expired
  auto now = std::chrono::steady_clock::now();
  if (now - entry->start_time > config.window_ns) {
    // Window expired, start new delegation
    entry = std::make_shared<DelegationEntry>();
    entry->unique_key = key;
    entry->start_time = now;
    shard.entries[key_hash] = entry;
    stats.delegations_created++;
    return {true, entry};
  }
  
  // Join existing delegation
  {
    std::lock_guard<std::mutex> entry_lock(entry->mtx);
    if (entry->state == DelegationState::COMPLETED) {
      // Already completed, return cached result
      callback(true, entry->result);
      stats.delegation_hits++;
      return {false, entry};
    }
    
    entry->waiters.push_back({op_id, callback});
  }
  
  stats.delegation_hits++;
  return {false, entry};
}

std::pair<bool, std::shared_ptr<DelegationEntry>>
DelegationTable::try_delegate_put(std::uint64_t key, std::uint64_t op_id,
                                  std::function<void()> write_op) {
  if (!config.enable) {
    return {true, nullptr};
  }

  std::uint64_t key_hash = std::hash<std::uint64_t>{}(key);
  auto& shard = shards[shard_for_key(key_hash)];
  
  std::lock_guard<std::mutex> lock(shard.mtx);
  
  auto it = shard.entries.find(key_hash);
  if (it == shard.entries.end()) {
    // First write for this key
    auto entry = std::make_shared<DelegationEntry>();
    entry->unique_key = key;
    entry->write_pending = true;
    entry->pending_writes.push_back(write_op);
    entry->start_time = std::chrono::steady_clock::now();
    shard.entries[key_hash] = entry;
    stats.delegations_created++;
    return {true, entry};
  }
  
  auto entry = it->second;
  
  // Handle hash collisions
  if (entry->unique_key != key) {
    if (config.collision_policy == Config::BYPASS) {
      stats.delegation_bypasses++;
      return {true, nullptr};
    }
  }
  
  // Check window expiry
  auto now = std::chrono::steady_clock::now();
  if (now - entry->start_time > config.window_ns) {
    // Start new delegation
    entry = std::make_shared<DelegationEntry>();
    entry->unique_key = key;
    entry->write_pending = true;
    entry->pending_writes.push_back(write_op);
    entry->start_time = now;
    shard.entries[key_hash] = entry;
    stats.delegations_created++;
    return {true, entry};
  }
  
  // Combine with existing writes
  {
    std::lock_guard<std::mutex> entry_lock(entry->mtx);
    entry->pending_writes.push_back(write_op);
    stats.write_combines++;
  }
  
  return {false, entry};
}

void DelegationTable::complete_delegation(std::uint64_t key_hash, bool success, const std::string& result) {
  auto& shard = shards[shard_for_key(key_hash)];
  
  std::shared_ptr<DelegationEntry> entry;
  {
    std::lock_guard<std::mutex> lock(shard.mtx);
    auto it = shard.entries.find(key_hash);
    if (it == shard.entries.end()) return;
    
    entry = it->second;
    shard.entries.erase(it); // Remove from active delegations
  }
  
  // Notify all waiters
  {
    std::lock_guard<std::mutex> entry_lock(entry->mtx);
    entry->state = success ? DelegationState::COMPLETED : DelegationState::FAILED;
    entry->result = result;
    
    for (auto& waiter : entry->waiters) {
      waiter.callback(success, result);
    }
    entry->waiters.clear();
  }
  
  entry->cv.notify_all();
}

void DelegationTable::cleanup_expired() {
  auto now = std::chrono::steady_clock::now();
  
  for (auto& shard : shards) {
    std::lock_guard<std::mutex> lock(shard.mtx);
    
    auto it = shard.entries.begin();
    while (it != shard.entries.end()) {
      if (now - it->second->start_time > config.window_ns * 2) { // 2x window for cleanup
        complete_delegation(it->first, false, "expired");
        it = shard.entries.erase(it);
      } else {
        ++it;
      }
    }
  }
}

} // namespace rdwc