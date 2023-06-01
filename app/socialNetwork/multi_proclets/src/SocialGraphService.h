#pragma once

#include <algorithm>
#include <cereal/types/set.hpp>
#include <future>
#include <iostream>
#include <iterator>
#include <nu/dis_hash_table.hpp>
#include <nu/proclet.hpp>
#include <set>
#include <string>
#include <thread>

#include "UserService.h"

namespace social_network {

class SocialGraphService {
public:
  constexpr static uint32_t kDefaultHashTablePowerNumShards = 9;

  SocialGraphService(nu::Proclet<UserService> proclet);
  std::vector<int64_t> GetFollowers(int64_t);
  std::vector<int64_t> GetFollowees(int64_t);
  void Follow(int64_t, int64_t);
  void Unfollow(int64_t, int64_t);
  void FollowWithUsername(std::string, std::string);
  void UnfollowWithUsername(std::string, std::string);

private:
 nu::Proclet<UserService> _user_service;
 nu::DistributedHashTable<int64_t, std::set<int64_t>, I64Hasher>
     _userid_to_followers_map;
 nu::DistributedHashTable<int64_t, std::set<int64_t>, I64Hasher>
     _userid_to_followees_map;
};

} // namespace social_network
