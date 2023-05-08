#pragma once

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <nu/utils/farmhash.hpp>
#include <string>

namespace social_network{
using json = nlohmann::json;

constexpr static auto kHashStrtoU64 = [](const std::string &str) {
  return util::Hash64(str);
};
constexpr static auto kHashI64toU64 = [](int64_t id) {
  return util::Hash64(reinterpret_cast<const char *>(&id), sizeof(int64_t));
};

int load_config_file(const std::string &file_name, json *config_json);
u_int16_t HashMacAddressPid(const std::string &mac);
std::string GetMachineId(std::string &netif);

} //namespace social_network
