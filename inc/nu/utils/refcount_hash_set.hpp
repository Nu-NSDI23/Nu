#pragma once

#include <sync.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nu/commons.hpp"
#include "nu/utils/robin_hood.h"

namespace nu {

template <typename K>
class RefcountHashSet {
 public:
  template <typename K1>
  void put(K1 &&k);
  template <typename K1>
  void remove(K1 &&k);
  // Can be only invoked once at a time.
  std::vector<K> all_keys();

 private:
  using V = int;

  robin_hood::unordered_map<K, V> ref_counts_[kNumCores];
};
}  // namespace nu

#include "nu/impl/refcount_hash_set.ipp"
