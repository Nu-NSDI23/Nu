#pragma once

#include <algorithm>
#include <cereal/types/set.hpp>
#include <future>
#include <iostream>
#include <iterator>
#include <nu/dis_hash_table.hpp>
#include <nu/rem_obj.hpp>
#include <set>
#include <string>
#include <thread>

#include "UserService.h"

namespace social_network {

class SocialGraphService {
public:
  constexpr static uint32_t kDefaultHashTablePowerNumShards = 9;

  SocialGraphService(nu::RemObj<UserService>::Cap);
  std::vector<int64_t> GetFollowers(int64_t);
  std::vector<int64_t> GetFollowees(int64_t);
  void Follow(int64_t, int64_t);
  void Unfollow(int64_t, int64_t);
  void FollowWithUsername(std::string, std::string);
  void UnfollowWithUsername(std::string, std::string);

private:
  nu::RemObj<UserService> _user_service_obj;
  nu::DistributedHashTable<int64_t, std::set<int64_t>, decltype(kHashI64toU64)>
      _userid_to_followers_map;
  nu::DistributedHashTable<int64_t, std::set<int64_t>, decltype(kHashI64toU64)>
      _userid_to_followees_map;
};

} // namespace social_network
