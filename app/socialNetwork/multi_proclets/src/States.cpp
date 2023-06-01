#include "States.hpp"

namespace social_network {

States::States()
    : username_to_userprofile_map(
          nu::make_dis_hash_table<std::string, UserProfile, StrHasher>(
              UserService::kDefaultHashTablePowerNumShards)),
      unique_id_service(nu::make_proclet<UniqueIdService>()),
      media_service(nu::make_proclet<MediaService>()),
      post_storage_service(nu::make_proclet<PostStorageService>()),
      user_timeline_service(nu::make_proclet<UserTimelineService>(
          std::forward_as_tuple(post_storage_service))),
      user_service(nu::make_proclet<UserService>(
          std::forward_as_tuple(username_to_userprofile_map))),
      social_graph_service(nu::make_proclet<SocialGraphService>(
          std::forward_as_tuple(user_service))),
      home_timeline_service(nu::make_proclet<HomeTimelineService>(
          std::forward_as_tuple(post_storage_service, social_graph_service))),
      url_shorten_service(nu::make_proclet<UrlShortenService>()),
      user_mention_service(nu::make_proclet<UserMentionService>(
          std::forward_as_tuple(username_to_userprofile_map))),
      text_service(nu::make_proclet<TextService>(
          std::forward_as_tuple(url_shorten_service, user_mention_service))),
      media_storage_service(nu::make_proclet<MediaStorageService>()) {}

}  // namespace social_network
