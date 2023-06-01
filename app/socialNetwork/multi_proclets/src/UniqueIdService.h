/*
 * 64-bit Unique Id Generator
 *
 * ------------------------------------------------------------------------
 * |0| 11 bit machine ID |      40-bit timestamp         | 12-bit counter |
 * ------------------------------------------------------------------------
 *
 * 11-bit machine Id code by hasing the MAC address
 * 40-bit UNIX timestamp in millisecond precision with custom epoch
 * 12 bit counter which increases monotonically on single process
 *
 */

#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <nu/utils/mutex.hpp>
#include <sstream>
#include <string>
extern "C" {
#include <runtime/timer.h>
#include <runtime/preempt.h>
}

#include "../gen-cpp/social_network_types.h"

namespace social_network {

class UniqueIdService {
public:
  UniqueIdService();
  int64_t ComposeUniqueId(const PostType::type post_type);
};

inline UniqueIdService::UniqueIdService() {}

inline int64_t UniqueIdService::ComposeUniqueId(PostType::type post_type) {
  return (rdtsc() << 8) | read_cpu();
}

} // namespace social_network
