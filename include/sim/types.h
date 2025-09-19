#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

typedef double SimTime; // microseconds

enum class Verb { READ, WRITE, CAS, SEND, RECV };

enum class Target { RNIC_ONCHIP, DRAM };

struct RdmaReq {
  Verb verb{};
  Target tgt{Target::DRAM};
  std::size_t bytes{0};
  int qp{0};
  int cs_id{0};
  int ms_id{0};
};

struct Completion { SimTime when{0.0}; };
