#pragma once

#include <cereal/archives/binary.hpp>
#include <nu/proclet.hpp>

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
  States(const States &states) = default;
  States(States &&states) = default;
  template <class Archive>
  void serialize(Archive &ar) {
    ar(username_to_userprofile_map, unique_id_service, media_service,
       post_storage_service, user_timeline_service, user_service,
       social_graph_service, home_timeline_service, url_shorten_service,
       user_mention_service, text_service, media_storage_service);
  }

  UserService::UserProfileMap username_to_userprofile_map;
  nu::Proclet<UniqueIdService> unique_id_service;
  nu::Proclet<MediaService> media_service;
  nu::Proclet<PostStorageService> post_storage_service;
  nu::Proclet<UserTimelineService> user_timeline_service;
  nu::Proclet<UserService> user_service;
  nu::Proclet<SocialGraphService> social_graph_service;
  nu::Proclet<HomeTimelineService> home_timeline_service;
  nu::Proclet<UrlShortenService> url_shorten_service;
  nu::Proclet<UserMentionService> user_mention_service;
  nu::Proclet<TextService> text_service;
  nu::Proclet<MediaStorageService> media_storage_service;
};

} // namespace social_network
