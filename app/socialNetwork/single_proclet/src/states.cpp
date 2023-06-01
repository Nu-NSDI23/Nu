#include "states.hpp"

namespace social_network {

States make_states() {
  States s;
  s.username_to_userprofile_map =
      nu::make_dis_hash_table<std::string, UserProfile, StrHasher>(
          States::kHashTablePowerNumShards);
  s.filename_to_data_map =
      nu::make_dis_hash_table<std::string, std::string, StrHasher>(
          States::kHashTablePowerNumShards);
  s.short_to_extended_map =
      nu::make_dis_hash_table<std::string, std::string, StrHasher>(
          States::kHashTablePowerNumShards);
  s.userid_to_hometimeline_map =
      nu::make_dis_hash_table<int64_t, Timeline, I64Hasher>(
          States::kHashTablePowerNumShards);
  s.userid_to_usertimeline_map =
      nu::make_dis_hash_table<int64_t, Timeline, I64Hasher>(
          States::kHashTablePowerNumShards);
  s.postid_to_post_map = nu::make_dis_hash_table<int64_t, Post, I64Hasher>(
      States::kHashTablePowerNumShards);
  s.userid_to_followers_map =
      nu::make_dis_hash_table<int64_t, std::set<int64_t>, I64Hasher>(
          States::kHashTablePowerNumShards);
  s.userid_to_followees_map =
      nu::make_dis_hash_table<int64_t, std::set<int64_t>, I64Hasher>(
          States::kHashTablePowerNumShards);
  return s;
}

}  // namespace social_network
