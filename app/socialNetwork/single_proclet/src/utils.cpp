#include <PicoSHA2/picosha2.h>
#include <fstream>
#include <iostream>
#include <jwt/jwt.hpp>
#include <nu/runtime.hpp>
#include <regex>
#include <sstream>
#include <string>
extern "C" {
#include <runtime/preempt.h>
}

#include "defs.hpp"
#include "utils.hpp"

using namespace jwt::params;

namespace social_network {

int LoadConfigFile(const std::string &file_name, json *config_json) {
  std::ifstream json_file;
  json_file.open(file_name);
  if (json_file.is_open()) {
    json_file >> *config_json;
    json_file.close();
    return 0;
  } else {
    std::cerr << "Cannot open service-config.json" << std::endl;
    return -1;
  }
}

RandomStringGenerator::RandomStringGenerator() {
  for (uint32_t i = 0; i < nu::kNumCores; i++) {
    gens_.emplace_back(rds_[i]());
    dists_.emplace_back(0, sizeof(kAlphaNum) - 2);
  }
}

std::string RandomStringGenerator::Gen(uint32_t len) {
  std::string s;
  auto core_num = get_cpu();
  for (int i = 0; i < len; ++i) {
    s += kAlphaNum[dists_[core_num](gens_[core_num])];
  }
  put_cpu();
  return s;
}

bool VerifyLogin(std::string &signature, const UserProfile &user_profile,
                 const std::string &username, const std::string &password,
                 const std::string &sec) {
  if (picosha2::hash256_hex_string(password + user_profile.salt) !=
      user_profile.password_hashed) {
    return false;
  }
  auto user_id_str = std::to_string(user_profile.user_id);
  auto timestamp_str =
      std::to_string(duration_cast<std::chrono::seconds>(
                         system_clock::now().time_since_epoch())
                         .count());
  jwt::jwt_object obj{algorithm("HS256"), secret(sec),
                      payload({{"user_id", user_id_str},
                               {"username", username},
                               {"timestamp", timestamp_str},
                               {"ttl", "3600"}})};
  signature = obj.signature();
  return true;
}

std::vector<std::string> MatchUrls(const std::string &text) {
  std::vector<std::string> urls;
  std::smatch m;
  std::regex e("(http://|https://)([a-zA-Z0-9_!~*'().&=+$%-]+)");
  auto s = text;
  while (std::regex_search(s, m, e)) {
    auto url = m.str();
    urls.emplace_back(url);
    s = m.suffix().str();
  }
  return urls;
}

std::vector<std::string> MatchMentions(const std::string &text) {
  std::vector<std::string> mention_usernames;
  std::smatch m;
  std::regex e("@[a-zA-Z0-9-_]+");
  auto s = text;
  while (std::regex_search(s, m, e)) {
    auto user_mention = m.str();
    user_mention = user_mention.substr(1, user_mention.length());
    mention_usernames.emplace_back(user_mention);
    s = m.suffix().str();
  }
  return mention_usernames;
}

std::string ShortenUrlInText(const std::string &text,
                             std::vector<Url> target_urls) {
  if (target_urls.empty()) {
    return text;
  }

  std::string updated_text;
  auto s = text;
  std::smatch m;
  std::regex e("(http://|https://)([a-zA-Z0-9_!~*'().&=+$%-]+)");
  int idx = 0;
  while (std::regex_search(s, m, e)) {
    updated_text += m.prefix().str() + target_urls[idx].shortened_url;
    s = m.suffix().str();
    idx++;
  }
  updated_text += s;
  return updated_text;
}

int64_t GenUniqueId() {
  auto core_id = read_cpu();
  auto tsc = rdtsc();
  return (tsc << 8) | core_id;
}

} // namespace social_network
