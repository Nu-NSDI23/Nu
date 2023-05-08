namespace nu {

template <typename T>
template <class Archive>
inline void RemUniquePtr<T>::save_move(Archive &ar) {
  RemPtr<T>::save(ar);
  release();
}

template <typename T>
consteval auto get_free_fn() {
  return +[](T &t) { delete &t; };
}

template <typename T>
inline RemUniquePtr<T>::RemUniquePtr() noexcept {}

template <typename T>
inline RemUniquePtr<T>::RemUniquePtr(T *raw_ptr) : RemPtr<T>(raw_ptr) {}

template <typename T>
inline RemUniquePtr<T>::RemUniquePtr(std::unique_ptr<T> &&unique_ptr) noexcept
    : RemUniquePtr(unique_ptr.get()) {
  unique_ptr.release();
}

template <typename T>
inline RemUniquePtr<T>::~RemUniquePtr() noexcept {
  reset();
}

template <typename T>
inline RemUniquePtr<T>::RemUniquePtr(RemUniquePtr<T> &&o) noexcept
    : RemPtr<T>(std::move(o)) {
  o.release();
}

template <typename T>
inline RemUniquePtr<T> &RemUniquePtr<T>::operator=(
    RemUniquePtr<T> &&o) noexcept {
  reset();
  RemPtr<T>::operator=(std::move(o));
  o.release();
  return *this;
}

template <typename T>
inline void RemUniquePtr<T>::release() {
  RemPtr<T>::raw_ptr_ = nullptr;
}

template <typename T>
inline void RemUniquePtr<T>::reset() {
  if (RemPtr<T>::get()) {
    RemPtr<T>::run(get_free_fn<T>());
    release();
  }
}

template <typename T>
inline Future<void> RemUniquePtr<T>::reset_async() {
  if (RemPtr<T>::get()) {
    auto future = RemPtr<T>::run_async(get_free_fn<T>());
    release();
    return future;
  } else {
    release();
    return run_async(+[](T &t) {});
  }
}

template <typename T, typename... Args>
inline RemUniquePtr<T> make_rem_unique(Args &&... args) {
  auto *raw_ptr = new (std::nothrow) T(std::forward<Args>(args)...);
  if (unlikely(!raw_ptr)) {
    return RemUniquePtr<T>();
  }
  return RemUniquePtr<T>(std::unique_ptr<T>(raw_ptr));
}

}  // namespace nu
