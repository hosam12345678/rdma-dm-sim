#pragma once
#include <cstdint>
#include <vector>
#include <array>

namespace hopscotch {

// Hopscotch hash overlay for B+ tree leaves (CHIME-style)
// Each leaf gets a small hopscotch table to accelerate key lookups
// before falling back to full leaf scan
class HopscotchOverlay {
public:
  static constexpr int DEFAULT_H = 16;  // neighborhood size
  static constexpr int DEFAULT_SLOTS = 32; // slots per leaf overlay
  
  struct Entry {
    std::uint64_t key{0};
    std::uint16_t leaf_slot{0}; // position in the actual B+ leaf
    bool valid{false};
    
    Entry() = default;
    Entry(std::uint64_t k, std::uint16_t slot) : key(k), leaf_slot(slot), valid(true) {}
  };
  
private:
  int H_; // neighborhood size 
  int num_slots_;
  std::vector<std::uint64_t> bitmap_; // neighborhood bitmaps (one per slot)
  std::vector<Entry> slots_; // the hash table slots
  
  // Hash function for keys within this overlay
  std::uint32_t hash_key(std::uint64_t key) const {
    // Simple hash - in practice could use the same hash as B+ tree
    return static_cast<std::uint32_t>((key * 0x9e3779b9) >> (32 - 5)); // log2(32) = 5
  }
  
public:
  HopscotchOverlay(int H = DEFAULT_H, int slots = DEFAULT_SLOTS) 
    : H_(H), num_slots_(slots), bitmap_(slots, 0), slots_(slots) {}
  
  // Insert a key->leaf_slot mapping into the hopscotch table
  // Returns true if successfully inserted, false if table full
  bool insert(std::uint64_t key, std::uint16_t leaf_slot);
  
  // Lookup a key in the hopscotch table
  // Returns leaf_slot if found, -1 if not found
  // If found, this gives the position in the actual B+ leaf to check
  int lookup(std::uint64_t key) const;
  
  // Remove a key from the hopscotch table
  void remove(std::uint64_t key);
  
  // Clear the entire overlay (e.g., when leaf splits/merges)
  void clear();
  
  // Get utilization stats for debugging/metrics
  double utilization() const;
  int num_entries() const;
  
  // Configuration getters
  int neighborhood_size() const { return H_; }
  int slot_count() const { return num_slots_; }
  
private:
  // Find an empty slot within neighborhood H of home bucket
  int find_empty_slot(int home) const;
  
  // Try to move entries to make space closer to home
  bool displace_closer(int empty_slot, int home);
  
  // Set/clear bit in neighborhood bitmap
  void set_bitmap_bit(int home, int slot);
  void clear_bitmap_bit(int home, int slot);
  bool get_bitmap_bit(int home, int slot) const;
};

// Configuration for hopscotch overlays
struct HopscotchConf {
  bool enable{false};
  int H{HopscotchOverlay::DEFAULT_H}; // neighborhood size
  int slots_per_leaf{HopscotchOverlay::DEFAULT_SLOTS}; // hash table size per leaf
  bool enable_speculative{true}; // speculative lookups without waiting for leaf lock
  int topK{8}; // only build overlays for top K hottest leaves
  double rebuild_threshold{0.7}; // rebuild overlay when utilization exceeds this
};

} // namespace hopscotch