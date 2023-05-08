#include <functional>
#include <utility>

extern "C" {
#include <base/assert.h>
#include <runtime/preempt.h>
}

namespace nu {

template <typename K>
template <typename K1>
inline void RefcountHashSet<K>::put(K1 &&k) {
  int cpu = get_cpu();
  auto &map = ref_counts_[cpu];
  auto iter = map.try_emplace(k, 0).first;
  if (++iter->second == 0) {
    map.erase(iter);
  }
  put_cpu();
}

template <typename K>
template <typename K1>
inline void RefcountHashSet<K>::remove(K1 &&k) {
  int cpu = get_cpu();
  auto &map = ref_counts_[cpu];
  auto iter = map.try_emplace(k, 0).first;
  if (--iter->second == 0) {
    map.erase(iter);
  }
  put_cpu();
}

template <typename K>
std::vector<K> RefcountHashSet<K>::all_keys() {
  std::unordered_map<K, V> sum_map;
  for (size_t i = 0; i < kNumCores; i++) {
    for (const auto &[k, cnt] : ref_counts_[i]) {
      sum_map[k] += cnt;
    }
  }

  std::vector<K> keys;
  for (auto &[k, cnt] : sum_map) {
    BUG_ON(cnt != 0 && cnt != 1);
    if (cnt) {
      keys.push_back(k);
    }
  }

  return keys;
}

}  // namespace nu
