#include "states.hpp"

namespace social_network {

States::States() {}

States make_states() {
  States states;

  states.username_to_userprofile_map =
      nu::make_sharded_ts_umap<std::string, UserProfile, StrHasher>();
  states.filename_to_data_map =
      nu::make_sharded_ts_umap<std::string, std::string, StrHasher>();
  states.short_to_extended_map =
      nu::make_sharded_ts_umap<std::string, std::string, StrHasher>();
  states.userid_to_hometimeline_map =
      nu::make_sharded_ts_umap<int64_t, Timeline, I64Hasher>();
  states.userid_to_usertimeline_map =
      nu::make_sharded_ts_umap<int64_t, Timeline, I64Hasher>();
  states.postid_to_post_map =
      nu::make_sharded_ts_umap<int64_t, Post, I64Hasher>();
  states.userid_to_followers_map =
      nu::make_sharded_ts_umap<int64_t, std::set<int64_t>, I64Hasher>();
  states.userid_to_followees_map =
      nu::make_sharded_ts_umap<int64_t, std::set<int64_t>, I64Hasher>();

  return states;
}

}  // namespace social_network
