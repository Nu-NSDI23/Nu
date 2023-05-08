#include <utility>

namespace cereal {

template <class T>
consteval bool is_memcpy_safe() {
  if constexpr (std::is_trivially_copy_assignable_v<T> &&
                !std::is_pointer_v<T> && !std::is_reference_v<T>) {
    return true;
  } else if constexpr (nu::is_specialization_of_v<T, std::pair>) {
    return is_memcpy_safe<typename T::first_type>() &&
           is_memcpy_safe<typename T::second_type>();
  } else if constexpr (nu::is_specialization_of_v<T, std::tuple>) {
    return []<std::size_t... ints>(std::integer_sequence<std::size_t, ints...> int_seq) {
      return (is_memcpy_safe<std::tuple_element_t<ints, T>>() && ...);
    }
    (std::make_index_sequence<std::tuple_size_v<T>>{});
  }
  return false;
}

template <class Archive, typename T,
          traits::EnableIf<cereal::traits::is_same_archive<
              Archive, cereal::BinaryOutputArchive>::value>>
inline void save(Archive &ar, T const &t) requires(
    is_memcpy_safe<T>() &&
    !HasBuiltinSerialize<Archive, T> &&
    !HasBuiltinSave<Archive, T> &&
    !HasBuiltinLoad<Archive, T> &&
    !cereal::common_detail::is_enum<T>::value &&
    !nu::is_specialization_of_v<T, cereal::BinaryData> &&
    !nu::is_specialization_of_v<T, std::tuple>) {
  ar(cereal::binary_data(&t, sizeof(T)));
}

template <class Archive, typename T,
          traits::EnableIf<cereal::traits::is_same_archive<
              Archive, cereal::BinaryOutputArchive>::value>>
inline void save_move(Archive &ar, T &&t) requires(
    is_memcpy_safe<T>() &&
    !HasBuiltinSerialize<Archive, T> &&
    !HasBuiltinSave<Archive, T> &&
    !HasBuiltinLoad<Archive, T> &&
    !cereal::common_detail::is_enum<T>::value &&
    !nu::is_specialization_of_v<T, cereal::BinaryData> &&
    !nu::is_specialization_of_v<T, std::tuple>) {
  ar(cereal::binary_data(&t, sizeof(T)));
}

template <class Archive, typename T,
          traits::EnableIf<cereal::traits::is_same_archive<
              Archive, cereal::BinaryInputArchive>::value>>
inline void load(Archive &ar, T &t) requires(
    is_memcpy_safe<T>() &&
    !HasBuiltinSerialize<Archive, T> &&
    !HasBuiltinSave<Archive, T> &&
    !HasBuiltinLoad<Archive, T> &&
    !cereal::common_detail::is_enum<T>::value &&
    !nu::is_specialization_of_v<T, cereal::BinaryData> &&
    !nu::is_specialization_of_v<T, std::tuple>) {
  ar(cereal::binary_data(&t, sizeof(T)));
}

template <typename... Types>
void serialize(
    cereal::BinaryInputArchive &ar,
    std::tuple<Types...> &t) requires(is_memcpy_safe<std::tuple<Types...>>()) {
  ar(cereal::binary_data(&t, sizeof(decltype(t))));
}

template <typename... Types>
void serialize(
    cereal::BinaryOutputArchive &ar,
    std::tuple<Types...> &t) requires(is_memcpy_safe<std::tuple<Types...>>()) {
  ar(cereal::binary_data(&t, sizeof(decltype(t))));
}

template <class Archive, typename P, typename A>
inline void save(Archive &ar, std::vector<P, A> const &v) requires(
    is_memcpy_safe<P>()) {
  ar(v.size());
  ar(cereal::binary_data(v.data(), v.size() * sizeof(P)));
}

template <class Archive, typename P, typename A>
inline void save_move(Archive &ar, std::vector<P, A> &&v) requires(
    is_memcpy_safe<P>()) {
  ar(v.size());
  ar(cereal::binary_data(v.data(), v.size() * sizeof(P)));
}

template <class Archive, typename P, typename A>
inline void load(Archive &ar, std::vector<P, A> &v) requires(
    is_memcpy_safe<P>()) {
  decltype(v.size()) size;
  ar(size);
  v.resize(size);
  ar(cereal::binary_data(v.data(), size * sizeof(P)));
}

template <typename T>
void SizeArchive::operator()(const T &t) {
  using D = std::decay_t<T>;

  if constexpr (nu::is_specialization_of_v<D, cereal::BinaryData>) {
    size += t.size;
  } else if constexpr (nu::is_specialization_of_v<D, cereal::NameValuePair>) {
    this->operator()(t.value);
  } else if constexpr (nu::is_specialization_of_v<D, cereal::SizeTag>) {
    size += sizeof(D);
  } else if constexpr (HasBuiltinSerialize<SizeArchive, D>) {
    const_cast<T &>(t).serialize(*this);
  } else if constexpr (HasBuiltinSave<SizeArchive, D>) {
    t.save(*this);
  } else if constexpr (is_memcpy_safe<D>()) {
    size += sizeof(D);
  } else {
    cereal::save(*this, t);
  }
}

template <typename... Ts>
void SizeArchive::operator()(const Ts &...ts) requires(sizeof...(Ts) > 1) {
  ((this->operator()(ts)), ...);
}

inline uint64_t get_size(const auto &t) {
  SizeArchive size_ar;
  size_ar(t);
  return size_ar.size;
}

}  // namespace cereal
