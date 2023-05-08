#pragma once

#include <cereal/archives/binary.hpp>
#include <nu/rem_obj.hpp>

#include "../gen-cpp/social_network_types.h"
#include "HomeTimelineService.h"
#include "MediaService.h"
#include "MediaStorageService.h"
#include "PostStorageService.h"
#include "SocialGraphService.h"
#include "TextService.h"
#include "UniqueIdService.h"
#include "UserService.h"
#include "UserTimelineService.h"

namespace social_network {

struct StateCaps;

struct States {
  States();
  States(const StateCaps &caps);
  StateCaps get_caps();

  UserService::UserProfileMap username_to_userprofile_map;
  nu::RemObj<UniqueIdService> unique_id_service_obj;
  nu::RemObj<MediaService> media_service_obj;
  nu::RemObj<PostStorageService> post_storage_service_obj;
  nu::RemObj<UserTimelineService> user_timeline_service_obj;
  nu::RemObj<UserService> user_service_obj;
  nu::RemObj<SocialGraphService> social_graph_service_obj;
  nu::RemObj<HomeTimelineService> home_timeline_service_obj;
  nu::RemObj<UrlShortenService> url_shorten_service_obj;
  nu::RemObj<UserMentionService> user_mention_service_obj;
  nu::RemObj<TextService> text_service_obj;
  nu::RemObj<MediaStorageService> media_storage_service_obj;
};

struct StateCaps {
  template <class Archive> void serialize(Archive &ar) {
    ar(username_to_userprofile_map_cap, unique_id_service_obj_cap,
       media_service_obj_cap, post_storage_service_obj_cap,
       user_timeline_service_obj_cap, user_service_obj_cap,
       social_graph_service_obj_cap, home_timeline_service_obj_cap,
       url_shorten_service_obj_cap, user_mention_service_obj_cap,
       text_service_obj_cap, media_storage_service_obj_cap);
  }

  UserService::UserProfileMap::Cap username_to_userprofile_map_cap;
  nu::RemObj<UniqueIdService>::Cap unique_id_service_obj_cap;
  nu::RemObj<MediaService>::Cap media_service_obj_cap;
  nu::RemObj<PostStorageService>::Cap post_storage_service_obj_cap;
  nu::RemObj<UserTimelineService>::Cap user_timeline_service_obj_cap;
  nu::RemObj<UserService>::Cap user_service_obj_cap;
  nu::RemObj<SocialGraphService>::Cap social_graph_service_obj_cap;
  nu::RemObj<HomeTimelineService>::Cap home_timeline_service_obj_cap;
  nu::RemObj<UrlShortenService>::Cap url_shorten_service_obj_cap;
  nu::RemObj<UserMentionService>::Cap user_mention_service_obj_cap;
  nu::RemObj<TextService>::Cap text_service_obj_cap;
  nu::RemObj<MediaStorageService>::Cap media_storage_service_obj_cap;
};
} // namespace social_network
