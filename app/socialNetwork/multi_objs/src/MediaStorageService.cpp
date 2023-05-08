#include "MediaStorageService.h"

namespace social_network {

MediaStorageService::MediaStorageService()
    : _filename_to_data_map(kDefaultHashTablePowerNumShards) {}

void MediaStorageService::UploadMedia(std::string filename, std::string data) {
  _filename_to_data_map.put(std::move(filename), std::move(data));
}

std::string MediaStorageService::GetMedia(std::string filename) {
  auto optional = _filename_to_data_map.get(std::move(filename));
  return optional.value_or("");
}

} // namespace social_network
