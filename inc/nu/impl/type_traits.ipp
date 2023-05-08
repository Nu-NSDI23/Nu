namespace nu {

class DistributedMemPool;
template <typename T>
class Proclet;
template <typename T>
class RemUniquePtr;
template <typename T>
class RemSharedPtr;

template <class T>
inline consteval bool is_safe_to_move() {
  return std::is_rvalue_reference_v<T> &&
         // TODO: add more safe-to-move types.
         (is_specialization_of_v<std::decay_t<T>, Proclet> ||
          is_specialization_of_v<std::decay_t<T>, RemUniquePtr> ||
          is_specialization_of_v<std::decay_t<T>, RemSharedPtr> ||
          std::is_same_v<std::decay_t<T>, DistributedMemPool>);
}

template <typename T>
inline T &&pass_across_proclet(T &&t) requires(is_safe_to_move<T &&>()) {
  return std::move(t);
}

template <typename T>
inline std::decay_t<T> pass_across_proclet(T &&t) requires DeepCopyAble<T> {
  if constexpr (std::is_rvalue_reference_v<T &&>) {
    T tmp = std::move(t);
    return tmp.deep_copy();
  } else {
    return t.deep_copy();
  }
}

template <typename T>
inline std::decay_t<T> pass_across_proclet(T &&t) {
  if constexpr (std::is_rvalue_reference_v<T &&>) {
    T tmp = std::move(t);
    return static_cast<T &>(tmp);
  } else {
    return t;
  }
}

}  // namespace nu
