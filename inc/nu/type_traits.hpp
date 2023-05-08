#pragma once

#include <type_traits>

namespace nu {

template <typename T, template <typename...> class Template>
struct is_specialization_of : std::false_type {};

template <template <typename...> class Template, typename... Args>
struct is_specialization_of<Template<Args...>, Template> : std::true_type {};

template <class T, template <class...> class Template>
constexpr bool is_specialization_of_v =
    is_specialization_of<T, Template>::value;

template <template <typename...> class C, typename... Ts>
std::true_type is_base_of_template_impl(const C<Ts...> *);

template <template <typename...> class C>
std::false_type is_base_of_template_impl(...);

template <typename T, template <typename...> class C>
using is_base_of_template =
    decltype(is_base_of_template_impl<C>(std::declval<T *>()));

template <typename T, template <typename...> class C>
inline constexpr bool is_base_of_template_v = is_base_of_template<T, C>::value;

template <typename T>
struct DeepDecay {
  template <typename U>
  struct DecayInner {
    using type = U;
  };

  template <template <typename...> class U, typename... Args>
  struct DecayInner<U<Args...>> {
    using type = U<std::decay_t<Args>...>;
  };

  using type = DecayInner<std::decay_t<T>>::type;
};

template <typename T>
using DeepDecay_t = typename DeepDecay<T>::type;

template <typename It>
struct iter_val {
  using Iter = std::decay_t<It>;
  using value_type = decltype(*Iter());
};

template <typename R>
using iter_val_t = iter_val<R>::value_type;

template <typename R>
struct range_iter {
  using Rng = std::decay_t<R>;
  using iter_type = decltype((Rng().begin()));
};

template <typename R>
using range_iter_t = range_iter<R>::iter_type;

template <std::size_t N, typename T, typename... types>
struct get_nth {
  using type = typename get_nth<N - 1, types...>::type;
};

template <typename T, typename... types>
struct get_nth<0, T, types...> {
  using type = T;
};

template <std::size_t N, typename... Args>
using get_nth_t = typename get_nth<N, Args...>::type;

template <class T>
concept PreIncrementable = requires(T t) {
  {++t};
};

template <class T>
concept PreDecrementable = requires(T t) {
  {--t};
};

template <class T>
concept DeepCopyAble = requires(T t) {
  { t.deep_copy() }
  ->std::same_as<T>;
};

template <class T>
consteval bool is_safe_to_move();

template <typename T>
T &&pass_across_proclet(T &&t) requires(is_safe_to_move<T &&>());

template <typename T>
std::decay_t<T> pass_across_proclet(T &&t) requires DeepCopyAble<T>;

template <typename T>
std::decay_t<T> pass_across_proclet(T &&t);

}  // namespace nu

#include "nu/impl/type_traits.ipp"
