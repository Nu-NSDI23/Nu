#pragma once

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <nu/utils/mutex.hpp>
#include <random>
#include <string>
#include <vector>

#include "../gen-cpp/social_network_types.h"

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::system_clock;
using json = nlohmann::json;

namespace social_network {

class RandomStringGenerator {
public:
  RandomStringGenerator();
  std::string Gen(uint32_t len);

private:
  constexpr static char kAlphaNum[] = "0123456789"
                                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "abcdefghijklmnopqrstuvwxyz";
  std::random_device rds_[nu::kNumCores];
  std::vector<std::mt19937> gens_;
  std::vector<std::uniform_int_distribution<int>> dists_;
};

std::vector<std::string> MatchUrls(const std::string &text);
std::vector<std::string> MatchMentions(const std::string &text);
std::string ShortenUrlInText(const std::string &text,
                             std::vector<Url> target_urls);
int LoadConfigFile(const std::string &file_name, json *config_json);
bool VerifyLogin(std::string &signature, const UserProfile &user_profile,
                 const std::string &username, const std::string &password,
                 const std::string &secret);
int64_t GenUniqueId();

} // namespace social_network
