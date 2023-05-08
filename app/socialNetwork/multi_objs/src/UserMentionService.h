#pragma once

#include <cstdint>
#include <string>

#include "../gen-cpp/social_network_types.h"
#include "UserService.h"

namespace social_network {

class UserMentionService {
public:
  UserMentionService(UserService::UserProfileMap::Cap cap)
      : _username_to_userprofile_map(std::move(cap)) {}
  std::vector<UserMention> ComposeUserMentions(std::vector<std::string>);

private:
  UserService::UserProfileMap _username_to_userprofile_map;
};

}  // namespace social_network
