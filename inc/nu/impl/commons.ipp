extern "C" {
#include <asm/ops.h>
}

#include <algorithm>
#include <limits>

namespace nu {

inline constexpr uint64_t bsr_64(uint64_t a) { return 63 - __builtin_clzll(a); }

inline constexpr ProcletHeader *to_proclet_header(ProcletID id) {
  return reinterpret_cast<ProcletHeader *>(id);
}

inline constexpr void *to_proclet_base(ProcletID id) {
  return reinterpret_cast<void *>(id);
}

inline constexpr ProcletID to_proclet_id(void *proclet_base) {
  return reinterpret_cast<ProcletID>(proclet_base);
}

inline constexpr SlabId_t to_slab_id(uint64_t proclet_base_addr) {
  return (proclet_base_addr - kMinProcletHeapVAddr) / kMinProcletHeapSize + 2;
}

inline constexpr SlabId_t to_slab_id(void *proclet_base) {
  return to_slab_id(reinterpret_cast<uint64_t>(proclet_base));
}

inline constexpr SlabId_t get_max_slab_id() {
  return to_slab_id(kMaxProcletHeapVAddr - kMinProcletHeapSize);
}

template <typename T>
inline std::span<std::byte> to_span(T &t) {
  return std::span<std::byte>(reinterpret_cast<std::byte *>(&t), sizeof(t));
}

template <typename T>
inline T &from_span(std::span<std::byte> span) {
  return *reinterpret_cast<T *>(span.data());
}

template <typename T>
inline const T &from_span(std::span<const std::byte> span) {
  return *reinterpret_cast<const T *>(span.data());
}

template <typename T>
inline constexpr T div_round_up_unchecked(T dividend, T divisor) {
  return (dividend + divisor - 1) / divisor;
}

inline constexpr uint64_t round_up_to_power2(uint64_t x) {
  return (1ULL << (bsr_64(x - 1) + 1));
}

template <typename T>
inline constexpr void ewma(double weight, T *result, T new_data) {
  if (*result == 0) {
    *result = new_data;
  } else {
    *result = weight * new_data + (1 - weight) * (*result);
  }
}

template <typename T>
void move_append_vector(std::vector<T> &dest, std::vector<T> &src) {
  std::move(src.begin(), src.end(), std::back_inserter(dest));
  src.clear();
}

template <typename K, typename V>
DIPair<K, V>::DIPair(const K &k, const V &v) : first(k), second(v) {}

template <typename K, typename V>
template <typename K1, typename V1>
DIPair<K, V>::DIPair(K1 &&k, V1 &&v)
    : first(std::forward<K1>(k)), second(std::forward<V1>(v)) {}

template <typename K, typename V>
template <class Archive>
void DIPair<K, V>::serialize(Archive &ar) {
  ar(first, second);
}

template <typename K, typename V>
bool DIPair<K, V>::operator==(const DIPair &o) const {
  return first == o.first;
}

template <typename K, typename V>
bool DIPair<K, V>::operator<(const DIPair &o) const {
  return first < o.first;
}

template <typename K, typename V>
bool DIPair<K, V>::operator>=(const DIPair &o) const {
  return first >= o.first;
}

template <typename K, typename V>
bool DIPair<K, V>::operator>(const DIPair &o) const {
  return first > o.first;
}

}  // namespace nu
