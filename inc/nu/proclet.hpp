#pragma once

#include <cstdint>
#include <optional>
#include <functional>

#include "nu/commons.hpp"
#include "nu/type_traits.hpp"
#include "nu/utils/future.hpp"

namespace nu {

template <typename T>
class WeakProclet;

template <typename T>
class Proclet;

template <typename... T>
concept ValidInvocationTypes = requires {
  requires(!std::is_reference_v<T> && ... && true);
  requires((!std::is_pointer_v<T> && ... && true));
  requires((!is_specialization_of_v<T, std::unique_ptr> && ... && true));
  requires((!is_specialization_of_v<T, std::shared_ptr> && ... && true));
  requires((!is_specialization_of_v<T, std::weak_ptr> && ... && true));
};

template <typename T>
class Proclet {
 public:
  Proclet(const Proclet &);
  Proclet &operator=(const Proclet &);
  Proclet(Proclet &&) noexcept;
  Proclet &operator=(Proclet &&) noexcept;
  Proclet();
  ~Proclet();
  operator bool() const;
  bool operator==(const Proclet &) const;
  ProcletID get_id() const;
  template <bool MigrEn = true, bool CPUSamp = true, typename RetT,
            typename... S0s, typename... S1s>
  Future<RetT> run_async(RetT (*fn)(T &, S0s...), S1s &&...states)
    requires ValidInvocationTypes<RetT, S0s...>;
  template <bool MigrEn = true, bool CPUSamp = true, typename RetT,
            typename... S0s, typename... S1s>
  RetT run(RetT (*fn)(T &, S0s...), S1s &&...states)
    requires ValidInvocationTypes<RetT, S0s...>;
  template <bool MigrEn = true, bool CPUSamp = true, typename RetT,
            typename... A0s, typename... A1s>
  Future<RetT> run_async(RetT (T::*md)(A0s...), A1s &&...args)
    requires ValidInvocationTypes<RetT, A0s...>;
  template <bool MigrEn = true, bool CPUSamp = true, typename RetT,
            typename... A0s, typename... A1s>
  RetT run(RetT (T::*md)(A0s...), A1s &&...args)
    requires ValidInvocationTypes<RetT, A0s...>;
  void reset();
  std::optional<Future<void>> reset_async();
  WeakProclet<T> get_weak() const;
  bool is_local() const;

  template <class Archive>
  void save(Archive &ar) const;
  template <class Archive>
  void save_move(Archive &ar);
  template <class Archive>
  void load(Archive &ar);

 private:
  ProcletID id_;

  template <typename U>
  friend class WeakProclet;
  template <typename U>
  friend class RemPtr;
  template <typename K, typename V, typename Hash, typename KeyEqual,
            uint64_t NumBuckets>
  friend class DistributedHashTable;
  friend class DistributedMemPool;
  friend int runtime_main_init(
      int argc, char **argv,
      std::function<void(int argc, char **argv)> main_func);

  std::optional<Future<void>> update_ref_cnt(ProcletID id, int delta);
  template <typename... S1s>
  static void invoke_remote(MigrationGuard &&caller_guard, ProcletID id,
                            S1s &&... states);
  template <typename RetT, typename... S1s>
  static RetT invoke_remote_with_ret(MigrationGuard &&caller_guard,
                                     ProcletID id, S1s &&... states);
  template <typename... As>
  static Proclet __create(bool pinned, uint64_t capacity, NodeIP ip_hint,
                          As &&... args);
  template <bool MigrEn = true, bool CPUSamp = true, typename RetT,
            typename... S0s, typename... S1s>
  Future<RetT> __run_async(RetT (*fn)(T &, S0s...), S1s &&...states);
  template <bool MigrEn = true, bool CPUSamp = true, typename RetT,
            typename... S0s, typename... S1s>
  RetT __run(RetT (*fn)(T &, S0s...), S1s &&...states);
  template <bool MigrEn = true, bool CPUSamp = true, typename RetT,
            typename... A0s, typename... A1s>
  Future<RetT> __run_async(RetT (T::*md)(A0s...), A1s &&...args);
  template <bool MigrEn = true, bool CPUSamp = true, typename RetT,
            typename... A0s, typename... A1s>
  RetT __run(RetT (T::*md)(A0s...), A1s &&...args);

  template <typename U, typename... As>
  friend Proclet<U> make_proclet(std::tuple<As...>, bool,
                                 std::optional<uint64_t>,
                                 std::optional<NodeIP>);
  template <typename U, typename... As>
  friend Future<Proclet<U>> make_proclet_async(std::tuple<As...>, bool,
                                               std::optional<uint64_t>,
                                               std::optional<NodeIP>);
  template <typename U>
  friend Proclet<U> make_proclet(bool, std::optional<uint64_t>,
                                 std::optional<NodeIP>);
  template <typename U>
  friend Future<Proclet<U>> make_proclet_async(bool, std::optional<uint64_t>,
                                               std::optional<NodeIP>);
};

template <typename T>
class WeakProclet : public Proclet<T> {
 public:
  WeakProclet();
  ~WeakProclet();
  WeakProclet(const Proclet<T> &proclet);
  WeakProclet(const WeakProclet<T> &proclet);
  WeakProclet &operator=(const WeakProclet<T> &proclet);

  template <class Archive>
  void save(Archive &ar) const;
  template <class Archive>
  void save_move(Archive &ar);
  template <class Archive>
  void load(Archive &ar);

 private:
  template <typename U>
  friend class RemPtr;
  friend class Runtime;

  WeakProclet(ProcletID id);
};

template <typename T, typename... As>
Proclet<T> make_proclet(std::tuple<As...> args_tuple, bool pinned = false,
                        std::optional<uint64_t> capacity = std::nullopt,
                        std::optional<uint32_t> ip_hint = std::nullopt);
template <typename T, typename... As>
Future<Proclet<T>> make_proclet_async(
    std::tuple<As...> args_tuple, bool pinned = false,
    std::optional<uint64_t> capacity = std::nullopt,
    std::optional<uint32_t> ip_hint = std::nullopt);
template <typename T>
Proclet<T> make_proclet(bool pinned = false,
                        std::optional<uint64_t> capacity = std::nullopt,
                        std::optional<uint32_t> ip_hint = std::nullopt);
template <typename T>
Future<Proclet<T>> make_proclet_async(
    bool pinned = false, std::optional<uint64_t> capacity = std::nullopt,
    std::optional<uint32_t> ip_hint = std::nullopt);

}  // namespace nu

#include "nu/impl/proclet.ipp"
