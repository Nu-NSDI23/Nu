#include "States.hpp"

namespace social_network {

States make_states() {
  States s;
  s.username_to_userprofile_map =
      nu::make_dis_hash_table<std::string, UserProfile, StrHasher>(
          UserService::kDefaultHashTablePowerNumShards);
  s.unique_id_service = nu::make_proclet<UniqueIdService>();
  s.media_service = nu::make_proclet<MediaService>();
  s.post_storage_service = nu::make_proclet<PostStorageService>();
  s.user_timeline_service = nu::make_proclet<UserTimelineService>(
      std::forward_as_tuple(s.post_storage_service));
  s.user_service = nu::make_proclet<UserService>(
      std::forward_as_tuple(s.username_to_userprofile_map));
  s.social_graph_service = nu::make_proclet<SocialGraphService>(
      std::forward_as_tuple(s.user_service));
  s.home_timeline_service = nu::make_proclet<HomeTimelineService>(
      std::forward_as_tuple(s.post_storage_service, s.social_graph_service));
  s.url_shorten_service = nu::make_proclet<UrlShortenService>();
  s.user_mention_service = nu::make_proclet<UserMentionService>(
      std::forward_as_tuple(s.username_to_userprofile_map));
  s.text_service = nu::make_proclet<TextService>(
      std::forward_as_tuple(s.url_shorten_service, s.user_mention_service));
  s.media_storage_service = nu::make_proclet<MediaStorageService>();
  return s;
}

}  // namespace social_network
