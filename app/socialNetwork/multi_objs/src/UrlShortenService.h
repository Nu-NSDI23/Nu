#pragma once

#include <chrono>
#include <nu/dis_hash_table.hpp>
#include <nu/utils/mutex.hpp>
#include <random>

#include "../gen-cpp/social_network_types.h"
#include "utils.h"

#define HOSTNAME "http://short-url/"

namespace social_network {

class UrlShortenService {
public:
  constexpr static uint32_t kDefaultHashTablePowerNumShards = 9;

  UrlShortenService();
  std::vector<Url> ComposeUrls(std::vector<std::string>);
  std::vector<std::string> GetExtendedUrls(std::vector<std::string>);
  void RemoveUrls(std::vector<std::string>);

private:
 nu::DistributedHashTable<std::string, std::string, StrHasher>
     _short_to_extended_map;

 std::string GenRandomStr(int length);
};

} // namespace social_network
