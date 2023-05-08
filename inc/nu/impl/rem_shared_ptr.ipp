namespace nu {

template <typename T>
template <class Archive>
inline void RemSharedPtr<T>::save(Archive &ar) const {
  auto copy = *this;
  copy.save_move(ar);
}

template <typename T>
template <class Archive>
inline void RemSharedPtr<T>::save_move(Archive &ar) {
  RemPtr<T>::save_move(ar);
  ar(shared_ptr_);
  RemPtr<T>::raw_ptr_ = nullptr;
}

template <typename T>
template <class Archive>
inline void RemSharedPtr<T>::load(Archive &ar) {
  RemPtr<T>::load(ar);
  ar(shared_ptr_);
}

template <typename T>
consteval auto get_reset_fn() {
  return +[](T &t, std::shared_ptr<T> *shared_ptr) { delete shared_ptr; };
}

template <typename T>
consteval auto get_copy_shared_ptr_fn() {
  return +[](T &t, std::shared_ptr<T> *shared_ptr) {
    return new std::shared_ptr<T>(*shared_ptr);
  };
}

template <typename T>
inline RemSharedPtr<T>::RemSharedPtr() noexcept {}

template <typename T>
inline RemSharedPtr<T>::RemSharedPtr(std::shared_ptr<T> &&shared_ptr) noexcept
    : RemPtr<T>(shared_ptr.get()),
      shared_ptr_(new std::shared_ptr<T>(std::move(shared_ptr))) {}

template <typename T>
inline RemSharedPtr<T>::RemSharedPtr(std::shared_ptr<T> *shared_ptr)
    : RemPtr<T>(shared_ptr ? shared_ptr->get() : nullptr),
      shared_ptr_(shared_ptr) {}

template <typename T>
inline RemSharedPtr<T>::~RemSharedPtr() noexcept {
  reset();
}

template <typename T>
inline RemSharedPtr<T>::RemSharedPtr(const RemSharedPtr<T> &o) noexcept
    : RemPtr<T>(o) {
  shared_ptr_ = RemPtr<T>::run(get_copy_shared_ptr_fn<T>(), o.shared_ptr_);
}

template <typename T>
inline RemSharedPtr<T> &RemSharedPtr<T>::operator=(
    const RemSharedPtr<T> &o) noexcept {
  reset();
  RemPtr<T>::operator=(o);
  shared_ptr_ = RemPtr<T>::run(get_copy_shared_ptr_fn<T>(), o.shared_ptr_);
  return *this;
}

template <typename T>
inline RemSharedPtr<T>::RemSharedPtr(RemSharedPtr<T> &&o) noexcept
    : RemPtr<T>(std::move(o)), shared_ptr_(o.shared_ptr_) {
  o.raw_ptr_ = nullptr;
}

template <typename T>
inline RemSharedPtr<T> &RemSharedPtr<T>::operator=(
    RemSharedPtr<T> &&o) noexcept {
  reset();
  RemPtr<T>::operator=(std::move(o));
  shared_ptr_ = o.shared_ptr_;
  o.raw_ptr_ = nullptr;
  return *this;
}

template <typename T>
inline void RemSharedPtr<T>::reset() {
  if (RemPtr<T>::get()) {
    RemPtr<T>::run(get_reset_fn<T>(), shared_ptr_);
    RemPtr<T>::raw_ptr_ = nullptr;
  }
}

template <typename T>
inline Future<void> RemSharedPtr<T>::reset_async() {
  if (RemPtr<T>::get()) {
    auto future = RemPtr<T>::run_async(get_reset_fn<T>(), shared_ptr_);
    RemPtr<T>::raw_ptr_ = nullptr;
    return future;
  } else {
    RemPtr<T>::raw_ptr_ = nullptr;
    return run_async(+[](T &t) {});
  }
}

template <typename T, typename... Args>
inline RemSharedPtr<T> make_rem_shared(Args &&... args) {
  auto *raw_ptr = new (std::nothrow) T(std::forward<Args>(args)...);
  if (unlikely(!raw_ptr)) {
    return RemSharedPtr<T>();
  }
  auto *shared_ptr = new (std::nothrow) std::shared_ptr<T>(raw_ptr);
  if (unlikely(!shared_ptr)) {
    return RemSharedPtr<T>();
  }
  return RemSharedPtr<T>(shared_ptr);
}

}  // namespace nu
