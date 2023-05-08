#include <sys/mman.h>
#include <algorithm>
#include <cstring>

#include "nu/stack_manager.hpp"
#include "nu/runtime.hpp"
#include "nu/rpc_client_mgr.hpp"

namespace nu {

void mmap_all_stack_space() {
  for (uint64_t vaddr = kMinStackClusterVAddr;
       vaddr + kStackClusterSize <= kMaxStackClusterVAddr;
       vaddr += kStackClusterSize) {
    auto *mmap_ptr = reinterpret_cast<uint8_t *>(vaddr);
    auto mmap_addr =
        mmap(mmap_ptr, kStackClusterSize, PROT_READ | PROT_WRITE,
             MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
    BUG_ON(mmap_addr != mmap_ptr);
    auto rc = madvise(mmap_ptr, kStackClusterSize, MADV_DONTDUMP);
    BUG_ON(rc == -1);
  }
}

StackManager::StackManager(VAddrRange stack_cluster)
    : range_(stack_cluster),
      cached_pool_([]() -> uint8_t * { BUG(); }, [](uint8_t *) {},
                   kPerCoreCacheSize) {
  mmap_all_stack_space();

  auto num_stacks = (stack_cluster.end - stack_cluster.start) / kStackSize;
  auto *ptr = reinterpret_cast<uint8_t *>(stack_cluster.start);
  for (uint64_t i = 0; i < num_stacks; i++) {
    ptr += kStackSize;
    cached_pool_.put(ptr);
  }
}

uint8_t *StackManager::get() { return cached_pool_.get(); }

bool StackManager::not_owned(uint8_t *stack) {
  auto addr = reinterpret_cast<uint64_t>(stack);
  return addr > range_.end || addr <= range_.start;
}

void StackManager::free(uint8_t *stack) {
  if (not_owned(stack)) {
    stack -= kStackSize;
    auto mmap_addr =
        mmap(stack, kStackSize, PROT_READ | PROT_WRITE,
             MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
    BUG_ON(mmap_addr != stack);
  }
}

void StackManager::put(uint8_t *stack) {
  if (unlikely(not_owned(stack))) {
    free(stack);
    auto ip = get_runtime()->caladan()->thread_get_creator_ip();
    auto *rpc_client = get_runtime()->rpc_client_mgr()->get_by_ip(ip);
    RPCReqGCStack rpc_req_gc_stack;
    rpc_req_gc_stack.stack = stack;
    RPCReturnBuffer return_buf;
    auto rc = rpc_client->Call(to_span(rpc_req_gc_stack), &return_buf);
    BUG_ON(rc != kOk);
    return;
  }
  cached_pool_.put(stack);
}

}  // namespace nu
