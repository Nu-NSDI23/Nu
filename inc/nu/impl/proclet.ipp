#include <array>
#include <concepts>
#include <cstdint>
#include <memory>
#include <sstream>
#include <type_traits>
#include <utility>
#include <unordered_map>

extern "C" {
#include <base/assert.h>
#include <runtime/net.h>
}

#include "nu/ctrl_client.hpp"
#include "nu/exception.hpp"
#include "nu/proclet_server.hpp"
#include "nu/rem_shared_ptr.hpp"
#include "nu/rem_unique_ptr.hpp"
#include "nu/rpc_server.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/future.hpp"

namespace nu {

struct ProcletHeader;

template <typename... S1s>
inline void serialize(auto *oa_sstream, S1s &&... states) {
  auto &ss = oa_sstream->ss;
  auto *rpc_type = const_cast<RPCReqType *>(
      reinterpret_cast<const RPCReqType *>(ss.view().data()));
  *rpc_type = kProcletCall;
  ss.seekp(sizeof(RPCReqType));

  auto &oa = oa_sstream->oa;
  ((oa << std::forward<S1s>(states)), ...);
}

template <typename T>
template <typename... S1s>
void Proclet<T>::invoke_remote(MigrationGuard &&caller_guard, ProcletID id,
                               S1s &&... states) {
  std::optional<MigrationGuard> optional_caller_guard;
  RuntimeSlabGuard slab_guard;

  auto *caller_header = get_runtime()->get_current_proclet_header();
  auto *oa_sstream = get_runtime()->archive_pool()->get_oa_sstream();
  serialize(oa_sstream, std::forward<S1s>(states)...);
  get_runtime()->detach();
  caller_guard.reset();

retry:
  auto states_view = oa_sstream->ss.view();
  auto states_data = reinterpret_cast<const std::byte *>(states_view.data());
  auto states_size = oa_sstream->ss.tellp();

  RPCReturnBuffer return_buf;
  RPCReturnCode rc;
  auto args_span = std::span(states_data, states_size);

  auto *client = get_runtime()->rpc_client_mgr()->get_by_proclet_id(id);
  rc = client->Call(args_span, &return_buf);

  if (unlikely(rc == kErrWrongClient)) {
    get_runtime()->rpc_client_mgr()->invalidate_cache(id, client);
    goto retry;
  }
  assert(rc == kOk);
  /*
  // metric logging
  // for local machine
  caller_header->spin_lock.lock();
  NodeIP target_ip = get_runtime()->rpc_client_mgr()->get_ip_by_proclet_id(id);

  auto target_kvpair = caller_header->remote_call_map.find(target_ip);
  if (target_kvpair != (caller_header->remote_call_map.end()) ){
    target_kvpair->second.first += 1;
    target_kvpair->second.second += states_size; 
  }
  else{
    caller_header->remote_call_map.emplace(target_ip, std::make_pair(1, states_size));
  }
  caller_header->spin_lock.unlock();
  // end metric logging */

  get_runtime()->archive_pool()->put_oa_sstream(oa_sstream);

  optional_caller_guard =
      get_runtime()->attach_and_disable_migration(caller_header);
  if (!optional_caller_guard) {
    Migrator::migrate_thread_and_ret_val<void>(
        std::move(return_buf), to_proclet_id(caller_header), nullptr, nullptr);
  }
}

template <typename T>
template <typename RetT, typename... S1s>
RetT Proclet<T>::invoke_remote_with_ret(MigrationGuard &&caller_guard,
                                        ProcletID id, S1s &&... states) {
  RetT ret;
  std::optional<MigrationGuard> optional_caller_guard;
  RuntimeSlabGuard slab_guard;

  auto *caller_header = get_runtime()->get_current_proclet_header();
  auto *oa_sstream = get_runtime()->archive_pool()->get_oa_sstream();
  serialize(oa_sstream, std::forward<S1s>(states)...);
  get_runtime()->detach();
  caller_guard.reset();

retry:
  auto states_view = oa_sstream->ss.view();
  auto states_data = reinterpret_cast<const std::byte *>(states_view.data());
  auto states_size = oa_sstream->ss.tellp();

  RPCReturnBuffer return_buf;
  RPCReturnCode rc;
  auto args_span = std::span(states_data, states_size);

  auto *client = get_runtime()->rpc_client_mgr()->get_by_proclet_id(id);
  rc = client->Call(args_span, &return_buf);

  if (unlikely(rc == kErrWrongClient)) {
    get_runtime()->rpc_client_mgr()->invalidate_cache(id, client);
    goto retry;
  }
  assert(rc == kOk);
  get_runtime()->archive_pool()->put_oa_sstream(oa_sstream);

  auto return_span = return_buf.get_mut_buf();
  /*
  // metric gathering
  caller_header->spin_lock.lock();
  NodeIP target_ip = get_runtime()->rpc_client_mgr()->get_ip_by_proclet_id(id);

  auto target_kvpair = caller_header->remote_call_map.find(target_ip);
  if (target_kvpair != (caller_header->remote_call_map.end()) ){
    target_kvpair->second.first += 1;
    target_kvpair->second.second += states_size + return_span.size_bytes(); 
  }
  else{
    caller_header->remote_call_map.emplace(target_ip, std::make_pair(
      1, states_size + return_span.size_bytes()));
  }
  caller_header->spin_lock.unlock();
  // end metric gathering*/

  optional_caller_guard =
      get_runtime()->attach_and_disable_migration(caller_header);
  if (!optional_caller_guard) {
    Migrator::migrate_thread_and_ret_val<RetT>(
        std::move(return_buf), to_proclet_id(caller_header), &ret, nullptr);
  } else {
    auto *ia_sstream = get_runtime()->archive_pool()->get_ia_sstream();
    auto &[ret_ss, ia] = *ia_sstream;
    // now calculated above during metrics gathering auto return_span = return_buf.get_mut_buf();
    ret_ss.span(
        {reinterpret_cast<char *>(return_span.data()), return_span.size()});
    if (caller_header) {
      ProcletSlabGuard slab_guard(&caller_header->slab);
      ia >> ret;
    } else {
      ia >> ret;
    }
    get_runtime()->archive_pool()->put_ia_sstream(ia_sstream);
  }

  return ret;
}

template <typename T>
inline Proclet<T>::Proclet() : id_(kNullProcletID) {}

template <typename T>
inline Proclet<T>::~Proclet() {
  reset();
}

template <typename T>
Proclet<T>::Proclet(const Proclet<T> &o)
    : id_(o.id_) {
  if (id_ != kNullProcletID) {
    auto inc_ref_optional = update_ref_cnt(id_, 1);
    if (inc_ref_optional) {
      inc_ref_optional->get();
    }
  }
}

template <typename T>
Proclet<T> &Proclet<T>::operator=(const Proclet<T> &o) {
  reset();
  id_ = o.id_;
  if (id_ != kNullProcletID) {
    auto inc_ref_optional = update_ref_cnt(id_, 1);
    if (inc_ref_optional) {
      inc_ref_optional->get();
    }
  }
  return *this;
}

template <typename T>
inline Proclet<T>::Proclet(Proclet<T> &&o) noexcept : id_(o.id_) {
  o.id_ = kNullProcletID;
}

template <typename T>
inline Proclet<T> &Proclet<T>::operator=(Proclet<T> &&o) noexcept {
  reset();
  id_ = o.id_;
  o.id_ = kNullProcletID;
  return *this;
}

template <typename T>
template <typename... As>
Proclet<T> Proclet<T>::__create(bool pinned, uint64_t capacity, NodeIP ip_hint,
                                As &&... args) {
  uint32_t server_ip;
  ProcletID callee_id;
  Proclet<T> callee_proclet;
  capacity = std::max(kMinProcletHeapSize, round_up_to_power2(capacity));
  BUG_ON(capacity > kMaxProcletHeapSize);

  ProcletHeader *caller_header;
  {
    MigrationGuard caller_migration_guard;

    caller_header = caller_migration_guard.header();
    get_runtime()->detach();
  }

  std::optional<MigrationGuard> optional_caller_migration_guard;
  {
    RuntimeSlabGuard slab_guard;

    auto optional =
        get_runtime()->controller_client()->allocate_proclet(capacity, ip_hint);
    if (unlikely(!optional)) {
      throw OutOfMemory();
    }
    std::tie(callee_id, server_ip) = *optional;
    get_runtime()->rpc_client_mgr()->update_cache(callee_id, server_ip);
    callee_proclet.id_ = callee_id;

    optional_caller_migration_guard =
        get_runtime()->attach_and_disable_migration(caller_header);
    if (!optional_caller_migration_guard) {
      RPCReturnBuffer return_buf;
      Migrator::migrate_thread_and_ret_val<void>(std::move(return_buf),
                                                 to_proclet_id(caller_header),
                                                 nullptr, nullptr);
      optional_caller_migration_guard = MigrationGuard();
    }
  }

  if (server_ip == get_cfg_ip()) {
    // Fast path: the proclet is actually local, use normal function call.
    ProcletServer::construct_proclet_locally<T, As...>(
        std::move(*optional_caller_migration_guard), to_proclet_base(callee_id),
        capacity, pinned, std::forward<As>(args)...);
    return callee_proclet;
  }

  // Cold path: use RPC.
  auto *handler = ProcletServer::construct_proclet<T, As...>;
  invoke_remote(std::move(*optional_caller_migration_guard), callee_id, handler,
                to_proclet_base(callee_id), capacity, pinned,
                std::forward<As>(args)...);
  return callee_proclet;
}

template <typename T>
inline Proclet<T>::operator bool() const {
  return id_;
}

template <typename T>
inline bool Proclet<T>::operator==(const Proclet &o) const {
  return id_ == o.id_;
}

template <typename T>
inline ProcletID Proclet<T>::get_id() const {
  return id_;
}

template <typename T>
template <bool MigrEn, bool CPUMon, bool CPUSamp, typename RetT,
          typename... S0s, typename... S1s>
inline Future<RetT> Proclet<T>::run_async(
    RetT (*fn)(T &, S0s...),
    S1s &&... states) requires ValidInvocationTypes<RetT, S0s...> {
  using fn_states_checker [[maybe_unused]] =
      decltype(fn(std::declval<T &>(), std::forward<S1s>(states)...));

  return __run_async<MigrEn, CPUMon, CPUSamp>(fn, std::forward<S1s>(states)...);
}

template <typename T>
template <bool MigrEn, bool CPUMon, bool CPUSamp, typename RetT,
          typename... S0s, typename... S1s>
inline Future<RetT> Proclet<T>::__run_async(RetT (*fn)(T &, S0s...),
                                            S1s &&... states) {
  return nu::async([&, fn, ... states = std::forward<S1s>(states)]() mutable {
    return __run<MigrEn, CPUMon, CPUSamp>(fn, std::forward<S1s>(states)...);
  });
}

template <typename T>
template <bool MigrEn, bool CPUMon, bool CPUSamp, typename RetT,
          typename... S0s, typename... S1s>
inline RetT Proclet<T>::run(
    RetT (*fn)(T &, S0s...),
    S1s &&... states) requires ValidInvocationTypes<RetT, S0s...> {
  using fn_states_checker [[maybe_unused]] =
      decltype(fn(std::declval<T &>(), std::move(states)...));

  return __run<MigrEn, CPUMon, CPUSamp>(fn, std::forward<S1s>(states)...);
}

template <typename T>
template <bool MigrEn, bool CPUMon, bool CPUSamp, typename RetT,
          typename... S0s, typename... S1s>
RetT Proclet<T>::__run(RetT (*fn)(T &, S0s...), S1s &&... states) {
  MigrationGuard caller_migration_guard;

  auto *caller_header = caller_migration_guard.header();
  if (caller_header) {
    auto callee_header = to_proclet_header(id_);
    auto optional_callee_migration_guard =
        get_runtime()->reattach_and_disable_migration(callee_header,
                                                      caller_migration_guard);
    if (optional_callee_migration_guard) {
      // Fast path: the callee proclet is actually local, use function call.

      constexpr auto kHasRetVal = !std::is_same_v<RetT, void>;
      std::conditional_t<kHasRetVal, RetT, ErasedType> ret;

      {
        ProcletSlabGuard slab_guard(&callee_header->slab);
        using StatesTuple = std::tuple<std::decay_t<S1s>...>;
        // Do copy for the most cases and only do move when we are sure it's
        // safe. For copy, we assume the type implements "deep copy".
        auto copied_states =
            reinterpret_cast<StatesTuple *>(alloca(sizeof(StatesTuple)));
        new (copied_states)
            StatesTuple(pass_across_proclet(std::forward<S1s>(states))...);
        caller_migration_guard.reset();

        // local call count recording for caller
        caller_header->local_call_cnt.inc_unsafe();
        // callee_header->local_call_cnt.inc_unsafe();

        if constexpr (kHasRetVal) {
          ProcletServer::run_closure_locally<MigrEn, CPUMon, CPUSamp, T, RetT,
                                             decltype(fn),
                                             std::decay_t<S1s>...>(
              &(*optional_callee_migration_guard), slab_guard, &ret,
              caller_header, callee_header, fn, copied_states);
        } else {
          ProcletServer::run_closure_locally<MigrEn, CPUMon, CPUSamp, T, RetT,
                                             decltype(fn),
                                             std::decay_t<S1s>...>(
              &(*optional_callee_migration_guard), slab_guard, nullptr,
              caller_header, callee_header, fn, copied_states);
        }
      }

      if constexpr (kHasRetVal) {
        return ret;
      } else {
        return;
      }
    }
  }

  // Slow path: the callee proclet is actually remote, use RPC.
  auto *handler = ProcletServer::run_closure<MigrEn, CPUMon, CPUSamp, T, RetT,
                                             decltype(fn), S1s...>;
  if constexpr (!std::is_same<RetT, void>::value) {
    return invoke_remote_with_ret<RetT>(std::move(caller_migration_guard), id_,
                                        handler, id_, fn,
                                        std::forward<S1s>(states)...);
  } else {
    invoke_remote(std::move(caller_migration_guard), id_, handler, id_, fn,
                  std::forward<S1s>(states)...);
  }
}

template <typename T>
template <bool MigrEn, bool CPUMon, bool CPUSamp, typename RetT,
          typename... A0s, typename... A1s>
inline Future<RetT> Proclet<T>::run_async(
    RetT (T::*md)(A0s...),
    A1s &&... args) requires ValidInvocationTypes<RetT, A0s...> {
  using md_args_checker [[maybe_unused]] =
      decltype((std::declval<T>().*(md))(std::move(args)...));

  return __run_async<MigrEn, CPUMon, CPUSamp>(md, std::forward<A1s>(args)...);
}

template <typename T>
template <bool MigrEn, bool CPUMon, bool CPUSamp, typename RetT,
          typename... A0s, typename... A1s>
inline Future<RetT> Proclet<T>::__run_async(RetT (T::*md)(A0s...),
                                            A1s &&... args) {
  return nu::async([&, md, ... args = std::forward<A1s>(args)]() mutable {
    return __run<MigrEn, CPUMon, CPUSamp>(md, std::forward<A1s>(args)...);
  });
}

template <typename T>
template <bool MigrEn, bool CPUMon, bool CPUSamp, typename RetT,
          typename... A0s, typename... A1s>
inline RetT Proclet<T>::run(
    RetT (T::*md)(A0s...),
    A1s &&... args) requires ValidInvocationTypes<RetT, A0s...> {
  using md_args_checker [[maybe_unused]] =
      decltype((std::declval<T>().*(md))(std::forward<A1s>(args)...));

  return __run<MigrEn, CPUMon, CPUSamp>(md, std::forward<A1s>(args)...);
}

template <typename T>
template <bool MigrEn, bool CPUMon, bool CPUSamp, typename RetT,
          typename... A0s, typename... A1s>
inline RetT Proclet<T>::__run(RetT (T::*md)(A0s...), A1s &&... args) {
  MethodPtr<decltype(md)> method_ptr;
  method_ptr.ptr = md;
  return __run<MigrEn, CPUMon, CPUSamp>(
      +[](T &t, decltype(method_ptr) method_ptr, A0s... args) {
        return (t.*(method_ptr.ptr))(std::move(args)...);
      },
      method_ptr, std::forward<A1s>(args)...);
}

template <typename T>
std::optional<Future<void>> Proclet<T>::update_ref_cnt(ProcletID id,
                                                       int delta) {
  {
    MigrationGuard caller_migration_guard;
    auto *caller_header = caller_migration_guard.header();

    if (caller_header) {
      auto *callee_header = to_proclet_header(id);
      auto optional_callee_migration_guard =
          get_runtime()->reattach_and_disable_migration(callee_header,
                                                        caller_migration_guard);
      caller_migration_guard.reset();
      if (optional_callee_migration_guard) {
        // Fast path: the proclet is actually local, use function call.
        ProcletServer::update_ref_cnt_locally<T>(
            &(*optional_callee_migration_guard), caller_header, callee_header,
            delta);
        return std::nullopt;
      }
    }
  }

  // Slow path: the proclet is actually remote, use RPC.
  return nu::async([&, id, delta]() mutable {
    MigrationGuard caller_migration_guard;
    auto *handler = ProcletServer::update_ref_cnt<T>;
    invoke_remote(std::move(caller_migration_guard), id, handler, id, delta);
  });
}

template <typename T>
void Proclet<T>::reset() {
  if (id_ != kNullProcletID) {
    auto dec_ref = update_ref_cnt(id_, -1);
    id_ = kNullProcletID;
    if (dec_ref) {
      dec_ref->get();
    }
  }
}

template <typename T>
std::optional<Future<void>> Proclet<T>::reset_async() {
  if (id_ != kNullProcletID) {
    auto ret = update_ref_cnt(id_, -1);
    id_ = kNullProcletID;
    return ret;
  }
  return std::nullopt;
}

template <typename T>
template <class Archive>
inline void Proclet<T>::save(Archive &ar) const {
  auto copy(*this);
  copy.save_move(ar);
}

template <typename T>
template <class Archive>
inline void Proclet<T>::save_move(Archive &ar) {
  ar(id_);
  id_ = kNullProcletID;
}

template <typename T>
template <class Archive>
inline void Proclet<T>::load(Archive &ar) {
  ar(id_);
}

template <typename T>
inline WeakProclet<T> Proclet<T>::get_weak() const {
  return WeakProclet<T>(*this);
}

template <typename T>
inline bool Proclet<T>::is_local() const {
  return to_proclet_header(id_)->status() == kPresent;
}

template <typename T>
inline WeakProclet<T>::WeakProclet() {}

template <typename T>
inline WeakProclet<T>::~WeakProclet() {
  this->id_ = kNullProcletID;
}

template <typename T>
inline WeakProclet<T>::WeakProclet(const Proclet<T> &proclet) : Proclet<T>() {
  this->id_ = proclet.id_;
}

template <typename T>
inline WeakProclet<T>::WeakProclet(const WeakProclet<T> &proclet)
    : Proclet<T>() {
  this->id_ = proclet.id_;
}

template <typename T>
inline WeakProclet<T> &WeakProclet<T>::operator=(
    const WeakProclet<T> &proclet) {
  this->id_ = proclet.id_;
  return *this;
}

template <typename T>
inline WeakProclet<T>::WeakProclet(ProcletID id) {
  this->id_ = id;
}

template <typename T>
template <class Archive>
inline void WeakProclet<T>::save(Archive &ar) const {
  ar(this->id_);
}

template <typename T>
template <class Archive>
inline void WeakProclet<T>::save_move(Archive &ar) {
  ar(this->id_);
  this->id_ = kNullProcletID;
}

template <typename T>
template <class Archive>
inline void WeakProclet<T>::load(Archive &ar) {
  ar(this->id_);
}

template <typename T, typename... As>
inline Proclet<T> make_proclet(std::tuple<As...> args_tuple, bool pinned,
                               std::optional<uint64_t> capacity,
                               std::optional<NodeIP> ip_hint) {
  return std::apply(
      [&](auto &&...args) {
        return Proclet<T>::__create(
            pinned, capacity.value_or(kDefaultProcletHeapSize),
            ip_hint.value_or(0), std::forward<As>(args)...);
      },
      std::move(args_tuple));
}

template <typename T, typename... As>
inline Future<Proclet<T>> make_proclet_async(std::tuple<As...> args_tuple,
                                             bool pinned,
                                             std::optional<uint64_t> capacity,
                                             std::optional<NodeIP> ip_hint) {
  return nu::async(
      [=] { return make_proclet<T>(args_tuple, pinned, capacity, ip_hint); });
}

template <typename T>
inline Proclet<T> make_proclet(bool pinned, std::optional<uint64_t> capacity,
                               std::optional<NodeIP> ip_hint) {
  return Proclet<T>::__create(
      pinned, capacity.value_or(kDefaultProcletHeapSize), ip_hint.value_or(0));
}

template <typename T>
inline Future<Proclet<T>> make_proclet_async(bool pinned,
                                             std::optional<uint64_t> capacity,
                                             std::optional<NodeIP> ip_hint) {
  return nu::async([=] { return make_proclet<T>(pinned, capacity, ip_hint); });
}

}  // namespace nu
