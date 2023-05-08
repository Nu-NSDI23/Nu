#include "States.hpp"

namespace social_network {

States::States()
    : username_to_userprofile_map(UserService::kDefaultHashTablePowerNumShards),
      unique_id_service_obj(nu::RemObj<UniqueIdService>::create()),
      media_service_obj(nu::RemObj<MediaService>::create()),
      post_storage_service_obj(nu::RemObj<PostStorageService>::create()),
      user_timeline_service_obj(nu::RemObj<UserTimelineService>::create(
          post_storage_service_obj.get_cap())),
      user_service_obj(nu::RemObj<UserService>::create(
          username_to_userprofile_map.get_cap())),
      social_graph_service_obj(
          nu::RemObj<SocialGraphService>::create(user_service_obj.get_cap())),
      home_timeline_service_obj(nu::RemObj<HomeTimelineService>::create(
          post_storage_service_obj.get_cap(),
          social_graph_service_obj.get_cap())),
      url_shorten_service_obj(nu::RemObj<UrlShortenService>::create()),
      user_mention_service_obj(nu::RemObj<UserMentionService>::create(
          username_to_userprofile_map.get_cap())),
      text_service_obj(
          nu::RemObj<TextService>::create(url_shorten_service_obj.get_cap(),
                                          user_mention_service_obj.get_cap())),
      media_storage_service_obj(nu::RemObj<MediaStorageService>::create()) {}

States::States(const StateCaps &caps)
    : username_to_userprofile_map(caps.username_to_userprofile_map_cap),
      unique_id_service_obj(caps.unique_id_service_obj_cap),
      media_service_obj(caps.media_service_obj_cap),
      post_storage_service_obj(caps.post_storage_service_obj_cap),
      user_timeline_service_obj(caps.user_timeline_service_obj_cap),
      user_service_obj(caps.user_service_obj_cap),
      social_graph_service_obj(caps.social_graph_service_obj_cap),
      home_timeline_service_obj(caps.home_timeline_service_obj_cap),
      url_shorten_service_obj(caps.url_shorten_service_obj_cap),
      user_mention_service_obj(caps.user_mention_service_obj_cap),
      text_service_obj(caps.text_service_obj_cap),
      media_storage_service_obj(caps.media_storage_service_obj_cap) {}

StateCaps States::get_caps() {
  StateCaps caps;
  caps.username_to_userprofile_map_cap = username_to_userprofile_map.get_cap();
  caps.unique_id_service_obj_cap = unique_id_service_obj.get_cap();
  caps.media_service_obj_cap = media_service_obj.get_cap();
  caps.post_storage_service_obj_cap = post_storage_service_obj.get_cap();
  caps.user_timeline_service_obj_cap = user_timeline_service_obj.get_cap();
  caps.user_service_obj_cap = user_service_obj.get_cap();
  caps.social_graph_service_obj_cap = social_graph_service_obj.get_cap();
  caps.home_timeline_service_obj_cap = home_timeline_service_obj.get_cap();
  caps.url_shorten_service_obj_cap = url_shorten_service_obj.get_cap();
  caps.user_mention_service_obj_cap = user_mention_service_obj.get_cap();
  caps.text_service_obj_cap = text_service_obj.get_cap();
  caps.media_storage_service_obj_cap = media_storage_service_obj.get_cap();
  return caps;
}

} // namespace social_network
