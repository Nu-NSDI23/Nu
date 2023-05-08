#pragma once

#include <nu/proclet.hpp>

#include "BackEndService.hpp"

namespace social_network {

class Initializer {
 public:
  Initializer(nu::Proclet<BackEndService> backend);
  void init();

 private:
  const std::string kFilePath =
      "datasets/social-graph/socfb-Princeton12/socfb-Princeton12.mtx";
  constexpr static uint32_t kTextLen = 64;
  constexpr static uint32_t kUrlLen = 64;
  constexpr static uint32_t kMinNumPostsPerUser = 1;
  constexpr static uint32_t kMaxNumPostsPerUser = 20;
  constexpr static uint32_t kMaxNumMentionsPerText = 2;
  constexpr static uint32_t kMaxNumUrlsPerText = 2;
  constexpr static uint32_t kMaxNumMediasPerText = 2;
  constexpr static uint32_t kConcurrency = 100;
  constexpr static bool kEnableCompose = false;
  nu::Proclet<BackEndService> backend_;
};

}  // namespace social_network
