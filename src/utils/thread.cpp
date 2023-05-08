#include "nu/runtime.hpp"
#include "nu/utils/cpu_load.hpp"
#include "nu/utils/thread.hpp"

namespace nu {

__attribute__((optimize("no-omit-frame-pointer"))) void
Thread::trampoline_in_proclet_env(void *args) {
  ProcletHeader *proclet_header;
  {
    Caladan::PreemptGuard g;

    proclet_header = get_runtime()->get_current_proclet_header();
    proclet_header->slab_ref_cnt.inc(g);
  }

  auto *d = reinterpret_cast<join_data *>(args);
  d->func();
  d->lock.lock();
  if (d->done) {
    d->cv.signal();
    d->lock.unlock();
  } else {
    d->done = true;
    d->cv.wait(&d->lock);
    d->lock.unlock();
  }
  delete d;

  {
    Caladan::PreemptGuard g;

    CPULoad::end_monitor();
    proclet_header->slab_ref_cnt.dec(g);
    get_runtime()->caladan()->thread_unset_owner_proclet(Caladan::thread_self(),
                                                         true);
  }

  auto runtime_stack_base =
      get_runtime()->caladan()->thread_get_runtime_stack_base();
  auto old_rsp = get_runtime()->switch_stack(runtime_stack_base);
  get_runtime()->switch_to_runtime_slab();

  auto proclet_stack_addr =
      ((reinterpret_cast<uintptr_t>(old_rsp) + kStackSize - 1) &
       (~(kStackSize - 1)));
  get_runtime()->stack_manager()->put(
      reinterpret_cast<uint8_t *>(proclet_stack_addr));
  get_runtime()->caladan()->thread_exit();
}

void Thread::trampoline_in_runtime_env(void *args) {
  auto *d = reinterpret_cast<join_data *>(args);

  d->func();
  d->lock.lock();
  if (d->done) {
    d->cv.signal();
    d->lock.unlock();
  } else {
    d->done = true;
    d->cv.wait(&d->lock);
    d->lock.unlock();
  }
  std::destroy_at(&d->func);
}

void Thread::join() {
  BUG_ON(!join_data_);

  join_data_->lock.lock();
  if (join_data_->done) {
    join_data_->cv.signal();
    join_data_->lock.unlock();
    join_data_ = nullptr;
    return;
  }

  join_data_->done = true;
  join_data_->cv.wait_and_unlock(&join_data_->lock);
  join_data_ = nullptr;
}

void Thread::detach() {
  BUG_ON(!join_data_);

  join_data_->lock.lock();
  if (join_data_->done) {
    join_data_->cv.signal();
    join_data_->lock.unlock();
    join_data_ = nullptr;
    return;
  }

  join_data_->done = true;
  join_data_->lock.unlock();
  join_data_ = nullptr;
}

}  // namespace nu
