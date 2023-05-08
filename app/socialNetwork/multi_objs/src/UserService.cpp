#include "UserService.h"

namespace social_network {

std::string GenRandomString(const int len) {
  static const std::string alphanum = "0123456789"
                                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "abcdefghijklmnopqrstuvwxyz";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(
      0, static_cast<int>(alphanum.length() - 1));
  std::string s;
  for (int i = 0; i < len; ++i) {
    s += alphanum[dist(gen)];
  }
  return s;
}

void UserService::LoadSecretAndMachineId() {
  json config_json;
  BUG_ON(load_config_file("config/service-config.json", &config_json) != 0);
  _secret = config_json["secret"];
  std::string netif = config_json["user-service"]["netif"];
  _machine_id = GetMachineId(netif);
  BUG_ON(_machine_id == "");
}

void UserService::RegisterUserWithId(std::string first_name,
                                     std::string last_name,
                                     std::string username, std::string password,
                                     int64_t user_id) {
  UserProfile user_profile;
  user_profile.first_name = std::move(first_name);
  user_profile.last_name = std::move(last_name);
  user_profile.user_id = user_id;
  user_profile.salt = GenRandomString(32);
  user_profile.password_hashed =
      picosha2::hash256_hex_string(std::move(password) + user_profile.salt);
  _username_to_userprofile_map.put(std::move(username),
                                   std::move(user_profile));
}

void UserService::RegisterUser(std::string first_name, std::string last_name,
                               std::string username, std::string password) {
  // Compose user_id
  auto core_id = read_cpu();
  auto tsc = rdtsc();
  auto user_id = (((tsc << 8) | core_id) << 16);
  RegisterUserWithId(std::move(first_name), std::move(last_name),
                     std::move(username), std::move(password), user_id);
}

Creator UserService::ComposeCreatorWithUsername(std::string username) {
  auto user_id_optional = _username_to_userprofile_map.get(username);
  BUG_ON(!user_id_optional);
  return ComposeCreatorWithUserId(user_id_optional->user_id,
                                  std::move(username));
}

Creator UserService::ComposeCreatorWithUserId(int64_t user_id,
                                              std::string username) {
  Creator creator;
  creator.username = std::move(username);
  creator.user_id = user_id;
  return creator;
}

std::variant<LoginErrorCode, std::string>
UserService::Login(std::string username, std::string password) {
  auto user_profile_optional = _username_to_userprofile_map.get(username);
  if (!user_profile_optional) {
    return NOT_REGISTERED;
  }
  auto &user_profile = *user_profile_optional;
  bool auth =
      (picosha2::hash256_hex_string(std::move(password) + user_profile.salt) ==
       user_profile.password_hashed);
  if (!auth) {
    return WRONG_PASSWORD;
  }
  auto user_id_str = std::to_string(user_profile.user_id);
  auto timestamp_str =
      std::to_string(duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count());
  jwt::jwt_object obj{algorithm("HS256"), secret(_secret),
                      payload({{"user_id", user_id_str},
                               {"username", username},
                               {"timestamp", timestamp_str},
                               {"ttl", "3600"}})};
  return obj.signature();
}

int64_t UserService::GetUserId(std::string username) {
  auto user_id_optional = _username_to_userprofile_map.get(std::move(username));
  BUG_ON(!user_id_optional);
  return user_id_optional->user_id;
}

} // namespace social_network
