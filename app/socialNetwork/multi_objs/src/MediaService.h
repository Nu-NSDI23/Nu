#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "../gen-cpp/social_network_types.h"

// 2018-01-01 00:00:00 UTC
#define CUSTOM_EPOCH 1514764800000

namespace social_network {

class MediaService {
public:
  std::vector<Media> ComposeMedia(std::vector<std::string> media_types,
                                  std::vector<int64_t> media_ids);
};

} // namespace social_network
