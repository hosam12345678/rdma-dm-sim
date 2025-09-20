#include "sim/hopscotch.h"
#include <algorithm>

namespace hopscotch {

bool HopscotchOverlay::insert(std::uint64_t key, std::uint16_t leaf_slot) {
  std::uint32_t home = hash_key(key) % num_slots_;
  
  // Check if key already exists (update case)
  for (int i = 0; i < H_; ++i) {
    int slot = (home + i) % num_slots_;
    if (get_bitmap_bit(home, slot) && slots_[slot].valid && slots_[slot].key == key) {
      slots_[slot].leaf_slot = leaf_slot; // update existing entry
      return true;
    }
  }
  
  // Find empty slot within neighborhood
  int empty_slot = find_empty_slot(home);
  if (empty_slot == -1) {
    // Try to displace entries to make space
    for (int candidate = home; candidate < home + H_ && candidate < num_slots_; ++candidate) {
      if (!slots_[candidate].valid && displace_closer(candidate, home)) {
        empty_slot = candidate;
        break;
      }
    }
  }
  
  if (empty_slot == -1) {
    return false; // table full, cannot insert
  }
  
  // Insert the new entry
  slots_[empty_slot] = Entry(key, leaf_slot);
  set_bitmap_bit(home, empty_slot);
  return true;
}

int HopscotchOverlay::lookup(std::uint64_t key) const {
  std::uint32_t home = hash_key(key) % num_slots_;
  
  // Check neighborhood bitmap and probe slots
  for (int i = 0; i < H_; ++i) {
    int slot = (home + i) % num_slots_;
    if (get_bitmap_bit(home, slot) && slots_[slot].valid && slots_[slot].key == key) {
      return slots_[slot].leaf_slot;
    }
  }
  
  return -1; // not found
}

void HopscotchOverlay::remove(std::uint64_t key) {
  std::uint32_t home = hash_key(key) % num_slots_;
  
  for (int i = 0; i < H_; ++i) {
    int slot = (home + i) % num_slots_;
    if (get_bitmap_bit(home, slot) && slots_[slot].valid && slots_[slot].key == key) {
      slots_[slot].valid = false;
      clear_bitmap_bit(home, slot);
      return;
    }
  }
}

void HopscotchOverlay::clear() {
  std::fill(bitmap_.begin(), bitmap_.end(), 0);
  for (auto& slot : slots_) {
    slot.valid = false;
  }
}

double HopscotchOverlay::utilization() const {
  int count = 0;
  for (const auto& slot : slots_) {
    if (slot.valid) count++;
  }
  return static_cast<double>(count) / num_slots_;
}

int HopscotchOverlay::num_entries() const {
  int count = 0;
  for (const auto& slot : slots_) {
    if (slot.valid) count++;
  }
  return count;
}

int HopscotchOverlay::find_empty_slot(int home) const {
  for (int i = 0; i < H_; ++i) {
    int slot = (home + i) % num_slots_;
    if (!slots_[slot].valid) {
      return slot;
    }
  }
  return -1;
}

bool HopscotchOverlay::displace_closer(int empty_slot, int home) {
  // Try to move entries from further away to this empty slot
  // to make space closer to home bucket
  for (int candidate = std::max(0, empty_slot - H_ + 1); candidate < empty_slot; ++candidate) {
    if (slots_[candidate].valid) {
      std::uint32_t candidate_home = hash_key(slots_[candidate].key) % num_slots_;
      
      // Check if we can move this entry to empty_slot and still be in its neighborhood
      int distance = (empty_slot - candidate_home + num_slots_) % num_slots_;
      if (distance < H_) {
        // Move the entry
        clear_bitmap_bit(candidate_home, candidate);
        set_bitmap_bit(candidate_home, empty_slot);
        slots_[empty_slot] = slots_[candidate];
        slots_[candidate].valid = false;
        return true;
      }
    }
  }
  return false;
}

void HopscotchOverlay::set_bitmap_bit(int home, int slot) {
  int bit_pos = (slot - home + num_slots_) % num_slots_;
  if (bit_pos < 64) { // safety check for bitmap size
    bitmap_[home] |= (1ULL << bit_pos);
  }
}

void HopscotchOverlay::clear_bitmap_bit(int home, int slot) {
  int bit_pos = (slot - home + num_slots_) % num_slots_;
  if (bit_pos < 64) {
    bitmap_[home] &= ~(1ULL << bit_pos);
  }
}

bool HopscotchOverlay::get_bitmap_bit(int home, int slot) const {
  int bit_pos = (slot - home + num_slots_) % num_slots_;
  if (bit_pos < 64) {
    return (bitmap_[home] & (1ULL << bit_pos)) != 0;
  }
  return false;
}

} // namespace hopscotch