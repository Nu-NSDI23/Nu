#pragma once

#include <cereal/types/string.hpp>
#include <cereal/types/variant.hpp>
#include <iomanip>
#include <iostream>
#include <jwt/jwt.hpp>
#include <nlohmann/json.hpp>
#include <nu/dis_hash_table.hpp>
#include <nu/proclet.hpp>
#include <nu/utils/mutex.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
extern "C" {
#include <runtime/preempt.h>
#include <runtime/timer.h>
}

#include "../gen-cpp/social_network_types.h"
#include "../third_party/PicoSHA2/picosha2.h"
#include "utils.h"

// Custom Epoch (January 1, 2018 Midnight GMT = 2018-01-01T00:00:00Z)
#define CUSTOM_EPOCH 1514764800000
#define MONGODB_TIMEOUT_MS 100

namespace social_network {

using json = nlohmann::json;
using namespace jwt::params;

std::string GenRandomString(const int len);

struct UserProfile {
  int64_t user_id;
  std::string first_name;
  std::string last_name;
  std::string salt;
  std::string password_hashed;

  template <class Archive> void serialize(Archive &ar) {
    ar(user_id, first_name, last_name, salt, password_hashed);
  }
};

enum LoginErrorCode { OK, NOT_REGISTERED, WRONG_PASSWORD };

class UserService {
public:
  constexpr static uint32_t kDefaultHashTablePowerNumShards = 9;
  using UserProfileMap =
      nu::DistributedHashTable<std::string, UserProfile, StrHasher>;

  UserService(UserProfileMap map)
      : _username_to_userprofile_map(std::move(map)) {
    LoadSecretAndMachineId();
  }
  void RegisterUser(std::string, std::string, std::string, std::string);
  void RegisterUserWithId(std::string, std::string, std::string, std::string,
                          int64_t);
  Creator ComposeCreatorWithUserId(int64_t, std::string);
  Creator ComposeCreatorWithUsername(std::string);
  std::variant<LoginErrorCode, std::string> Login(std::string, std::string);
  int64_t GetUserId(std::string);

private:
  std::string _machine_id;
  std::string _secret;
  UserProfileMap _username_to_userprofile_map;
  nu::Mutex _mutex;

  void LoadSecretAndMachineId();
};

} // namespace social_network
