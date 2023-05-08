#include "nu/dis_mem_pool.hpp"

namespace nu {

void DistributedMemPool::__check_probing(uint64_t cur_us) {
  ScopedLock<Mutex> scope(&global_mutex_);
  if (likely(!probing_active_ && !done_)) {
    last_probing_us_ = cur_us;
    if (probing_thread_) {
      probing_thread_->join();
    }
    probing_active_ = true;
    probing_thread_.reset(new Thread([&] { probing_fn(); }));
  }
}

void DistributedMemPool::probing_fn() {
  auto num_probes = global_full_shards_.size();
  while (num_probes-- && !ACCESS_ONCE(done_)) {
    global_mutex_.lock();
    auto full_shard = std::move(global_full_shards_.front());
    global_full_shards_.pop_front();
    global_mutex_.unlock();
    if (full_shard.proclet.run(&Heap::has_space_for,
                               full_shard.failed_alloc_size)) {
      ScopedLock<Mutex> scope(&global_mutex_);
      global_free_shards_.emplace_back(std::move(full_shard.proclet));
    }
  }
  probing_active_ = false;
}

void DistributedMemPool::__handle_local_free_shard_full() {
  auto &free_shard_optional = local_free_shards_[read_cpu()].shard;
  auto free_shard = std::move(*free_shard_optional);
  free_shard_optional = std::nullopt;
  put_cpu();

  ScopedLock<Mutex> scope(&global_mutex_);
  global_full_shards_.emplace_back(std::move(free_shard));
}

void DistributedMemPool::__handle_no_local_free_shard() {
  put_cpu();

  global_mutex_.lock();
  if (unlikely(global_free_shards_.empty())) {
    global_free_shards_.emplace_back(make_proclet<Heap>(false, kShardSize));
  }
  auto free_shard = std::move(global_free_shards_.front());
  global_free_shards_.pop_front();
  global_mutex_.unlock();

  auto cpu = get_cpu();
  auto &free_shard_optional = local_free_shards_[cpu].shard;
  if (likely(!free_shard_optional)) {
    free_shard_optional = std::move(free_shard);
    put_cpu();
  } else {
    put_cpu();
    ScopedLock<Mutex> scope(&global_mutex_);
    global_free_shards_.emplace_back(std::move(free_shard));
  }
}

}  // namespace nu
