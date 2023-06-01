#pragma once

#include <nu/dis_hash_table.hpp>
#include <string>

#include "../gen-cpp/social_network_types.h"
#include "utils.h"

namespace social_network {

class MediaStorageService {
public:
  constexpr static uint32_t kDefaultHashTablePowerNumShards = 9;

  MediaStorageService();
  void UploadMedia(std::string filename, std::string data);
  std::string GetMedia(std::string filename);

private:
 nu::DistributedHashTable<std::string, std::string, StrHasher>
     _filename_to_data_map;
};

} // namespace social_network
