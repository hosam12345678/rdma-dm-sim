#pragma once
#include <queue>
#include <functional>

struct Event {
  double t;
  std::function<void()> fn;
  bool operator<(const Event& other) const { return t > other.t; } // min-heap
};

struct EventLoop {
  double now{0.0};
  std::priority_queue<Event> pq;
  void at(double t, std::function<void()> fn) { pq.push(Event{t, std::move(fn)}); }
  void after(double dt, std::function<void()> fn) { at(now + dt, std::move(fn)); }
  void run() { while (!pq.empty()) { auto e = pq.top(); pq.pop(); now = e.t; e.fn(); } }
};
