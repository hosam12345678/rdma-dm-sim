#pragma once
#include <list>
#include <cstdint>
#include <unordered_map>
#include <functional>

struct CacheKey { std::uint64_t node_id; int level; };
struct CacheKeyHash { std::size_t operator()(CacheKey const& k) const { return std::hash<std::uint64_t>()((k.node_id<<3) ^ k.level); } };
struct CacheKeyEq { bool operator()(CacheKey const&a, CacheKey const& b) const { return a.node_id==b.node_id && a.level==b.level; } };

struct LRUCache {
  std::size_t cap_bytes, cur_bytes{0};
  std::list<CacheKey> lru;
  std::unordered_map<CacheKey, std::list<CacheKey>::iterator, CacheKeyHash, CacheKeyEq> pos;
  std::unordered_map<CacheKey, std::size_t, CacheKeyHash, CacheKeyEq> sz;
  explicit LRUCache(std::size_t cap): cap_bytes(cap) {}
  bool get(CacheKey k){ auto it=pos.find(k); if(it==pos.end()) return false; lru.splice(lru.begin(), lru, it->second); return true; }
  void put(CacheKey k, std::size_t bytes){ if(pos.count(k)){ lru.splice(lru.begin(), lru, pos[k]); return; } lru.push_front(k); pos[k]=lru.begin(); sz[k]=bytes; cur_bytes+=bytes; while(cur_bytes>cap_bytes){ auto ev=lru.back(); lru.pop_back(); cur_bytes-=sz[ev]; sz.erase(ev); pos.erase(ev);} }
};
