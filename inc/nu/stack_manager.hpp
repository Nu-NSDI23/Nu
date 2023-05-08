#pragma once

#include <sync.h>

#include <cstdint>
#include <map>

#include "nu/commons.hpp"
#include "nu/rpc_server.hpp"
#include "nu/utils/cached_pool.hpp"

namespace nu {

struct RPCReqGCStack {
  RPCReqType rpc_type = kGCStack;
  uint8_t *stack;
} __attribute__((packed));

class StackManager {
 public:
  constexpr static uint32_t kPerCoreCacheSize = 32;

  StackManager(VAddrRange stack_cluster);
  uint8_t *get();
  void put(uint8_t *stack);
  void free(uint8_t *stack);

 private:
  struct alignas(kCacheLineBytes) CoreCache {
    uint8_t *stack;
  };

  VAddrRange range_;
  CachedPool<uint8_t> cached_pool_;

  bool not_owned(uint8_t *stack);
};

}  // namespace nu

