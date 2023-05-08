#include "states.hpp"

namespace social_network {

States::States()
    : username_to_userprofile_map(
          nu::make_dis_hash_table<std::string, UserProfile, StrHasher>(
              kHashTablePowerNumShards)),
      filename_to_data_map(
          nu::make_dis_hash_table<std::string, std::string, StrHasher>(
              kHashTablePowerNumShards)),
      short_to_extended_map(
          nu::make_dis_hash_table<std::string, std::string, StrHasher>(
              kHashTablePowerNumShards)),
      userid_to_hometimeline_map(
          nu::make_dis_hash_table<int64_t, Timeline, I64Hasher>(
              kHashTablePowerNumShards)),
      userid_to_usertimeline_map(
          nu::make_dis_hash_table<int64_t, Timeline, I64Hasher>(
              kHashTablePowerNumShards)),
      postid_to_post_map(nu::make_dis_hash_table<int64_t, Post, I64Hasher>(
          kHashTablePowerNumShards)),
      userid_to_followers_map(
          nu::make_dis_hash_table<int64_t, std::set<int64_t>, I64Hasher>(
              kHashTablePowerNumShards)),
      userid_to_followees_map(
          nu::make_dis_hash_table<int64_t, std::set<int64_t>, I64Hasher>(
              kHashTablePowerNumShards)) {}

}  // namespace social_network
