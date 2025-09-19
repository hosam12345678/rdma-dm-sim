#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>

struct Zipf {
  std::uint64_t n; double s; std::vector<double> cdf;
  Zipf(std::uint64_t n_, double s_) : n(n_), s(s_) {
    if (n==0) n=1;
    std::vector<double> w(n);
    for (std::uint64_t i=1;i<=n;i++) w[i-1]=1.0/std::pow((double)i, s>0? s:0.0001);
    double sum=0; for(double x: w) sum+=x;
    double run=0; cdf.resize(n);
    for (std::uint64_t i=0;i<n;i++){ run += w[i]/sum; cdf[i]=run; }
  }
  std::uint64_t sample(double u) const { auto it = std::lower_bound(cdf.begin(), cdf.end(), u); return (std::uint64_t)std::distance(cdf.begin(), it); }
};
