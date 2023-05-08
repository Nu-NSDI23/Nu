#include "nu/utils/zipf.hpp"

#include <cmath>
#include <memory>

namespace nu {

zipf_distribution::zipf_distribution(uint64_t num, double q)
    : num_(num), q_(q) {
  pdf_.reserve(num);
  for (uint64_t i = 0; i < num; i++) {
    pdf_.push_back(std::pow(static_cast<double>(i + 1), -q));
  }
  std::construct_at(&dist_, pdf_.begin(), pdf_.end());
}

uint64_t zipf_distribution::operator()(std::mt19937 &rng) { return dist_(rng); }

uint64_t zipf_distribution::min() const { return 0; }

uint64_t zipf_distribution::max() const { return num_ - 1; }

}  // namespace nu
