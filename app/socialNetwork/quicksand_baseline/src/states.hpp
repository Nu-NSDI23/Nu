#pragma once

#include <cereal/types/string.hpp>
#include <nu/dis_hash_table.hpp>
#include <nu/utils/farmhash.hpp>

#include "defs.hpp"
#include "utils.hpp"

namespace social_network {

struct StrHasher {
  uint64_t operator()(const std::string &str) { return util::Hash64(str); }
};

struct I64Hasher {
  uint64_t operator()(const int64_t &id) {
    return util::Hash64(reinterpret_cast<const char *>(&id), sizeof(int64_t));
  }
};

struct States {
  States();
  States(const States &) noexcept = default;
  States(States &&) noexcept = default;
  States &operator=(const States &) noexcept = default;
  States &operator=(States &&) noexcept = default;

  constexpr static uint32_t kHashTablePowerNumShards = 9;

  template <class Archive>
  void serialize(Archive &ar) {
    ar(username_to_userprofile_map, filename_to_data_map, short_to_extended_map,
       userid_to_hometimeline_map, userid_to_usertimeline_map,
       postid_to_post_map, userid_to_followers_map, userid_to_followees_map,
       secret);
  }

  nu::DistributedHashTable<std::string, UserProfile, StrHasher>
      username_to_userprofile_map;
  nu::DistributedHashTable<std::string, std::string, StrHasher>
      filename_to_data_map;
  nu::DistributedHashTable<std::string, std::string, StrHasher>
      short_to_extended_map;
  nu::DistributedHashTable<int64_t, Timeline, I64Hasher>
      userid_to_hometimeline_map;
  nu::DistributedHashTable<int64_t, Timeline, I64Hasher>
      userid_to_usertimeline_map;
  nu::DistributedHashTable<int64_t, Post, I64Hasher> postid_to_post_map;
  nu::DistributedHashTable<int64_t, std::set<int64_t>, I64Hasher>
      userid_to_followers_map;
  nu::DistributedHashTable<int64_t, std::set<int64_t>, I64Hasher>
      userid_to_followees_map;
  std::string secret;
};

} // namespace social_network
