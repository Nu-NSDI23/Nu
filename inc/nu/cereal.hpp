#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/details/traits.hpp>
#include <cereal/types/deque.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/queue.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/stack.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/unordered_set.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>
#include <concepts>
#include <type_traits>

#include "nu/type_traits.hpp"

namespace cereal {

template <class Archive, class T>
concept HasBuiltinSerialize = requires(Archive ar, T t) {
    { t.serialize(ar) };
};

template <class Archive, class T>
concept HasBuiltinSave = requires(Archive ar, const T &t) {
    { t.save(ar) };
};

template <class Archive, class T>
concept HasBuiltinLoad = requires(Archive ar, T t) {
    { t.load(ar) };
};

template <class T>
consteval bool is_memcpy_safe();

template <class Archive, typename T,
          traits::EnableIf<cereal::traits::is_same_archive<
              Archive, cereal::BinaryOutputArchive>::value> = traits::sfinae>
void save(Archive &ar, T const &t) requires(
    is_memcpy_safe<T>() &&
    !HasBuiltinSerialize<Archive, T> &&
    !HasBuiltinSave<Archive, T> &&
    !HasBuiltinLoad<Archive, T> &&
    !cereal::common_detail::is_enum<T>::value &&
    !nu::is_specialization_of_v<T, cereal::BinaryData> &&
    !nu::is_specialization_of_v<T, std::tuple>);

template <class Archive, typename T,
          traits::EnableIf<cereal::traits::is_same_archive<
              Archive, cereal::BinaryOutputArchive>::value> = traits::sfinae>
void save_move(Archive &ar, T &&t) requires(
    is_memcpy_safe<T>() &&
    !HasBuiltinSerialize<Archive, T> &&
    !HasBuiltinSave<Archive, T> &&
    !HasBuiltinLoad<Archive, T> &&
    !cereal::common_detail::is_enum<T>::value &&
    !nu::is_specialization_of_v<T, cereal::BinaryData> &&
    !nu::is_specialization_of_v<T, std::tuple>);

template <class Archive, typename T,
          traits::EnableIf<cereal::traits::is_same_archive<
              Archive, cereal::BinaryInputArchive>::value> = traits::sfinae>
void load(Archive &ar, T &t) requires(
    is_memcpy_safe<T>() &&
    !HasBuiltinSerialize<Archive, T> &&
    !HasBuiltinSave<Archive, T> &&
    !HasBuiltinLoad<Archive, T> &&
    !cereal::common_detail::is_enum<T>::value &&
    !nu::is_specialization_of_v<T, cereal::BinaryData> &&
    !nu::is_specialization_of_v<T, std::tuple>);

template <typename... Types>
void serialize(cereal::BinaryOutputArchive &ar, std::tuple<Types...> &t) requires(
    is_memcpy_safe<std::tuple<Types...>>());

template <typename... Types>
void serialize(cereal::BinaryInputArchive &ar, std::tuple<Types...> &t) requires(
    is_memcpy_safe<std::tuple<Types...>>());

template <class Archive, typename P, typename A>
void save(Archive &ar, std::vector<P, A> const &v) requires(
    is_memcpy_safe<P>());

template <class Archive, typename P, typename A>
void save_move(Archive &ar, std::vector<P, A> &&v) requires(
    is_memcpy_safe<P>());

template <class Archive, typename P, typename A>
void load(Archive &ar, std::vector<P, A> &v) requires(
    is_memcpy_safe<P>());

struct SizeArchive {
  template <typename T>
  void operator()(const T &t);
  template <typename... Ts>
  void operator()(const Ts &...ts) requires(sizeof...(Ts) > 1);

  uint64_t size = 0;
};

uint64_t get_size(const auto &t);

namespace traits {

template <class T>
struct is_output_serializable<T, SizeArchive>
    : std::integral_constant<bool, true> {};

}  // namespace traits

}  // namespace cereal

#include "nu/impl/cereal.ipp"
