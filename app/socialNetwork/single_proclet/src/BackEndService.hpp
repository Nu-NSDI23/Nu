#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/types/set.hpp>
#include <string>
#include <variant>
#include <vector>

#include "../gen-cpp/BackEndService.h"
#include "../gen-cpp/social_network_types.h"
#include "defs.hpp"
#include "states.hpp"
#include "utils.hpp"

namespace social_network {

class BackEndService {
public:
  BackEndService(States states);
  void ComposePost(const std::string &username, int64_t user_id,
                   const std::string &text,
                   const std::vector<int64_t> &media_ids,
                   const std::vector<std::string> &media_types,
                   PostType::type post_type);
  std::vector<Post> ReadUserTimeline(int64_t user_id, int start, int stop);
  std::variant<LoginErrorCode, std::string> Login(const std::string &username,
                                                  const std::string &password);
  void RegisterUser(const std::string &first_name, const std::string &last_name,
                    const std::string &username, const std::string &password);
  void RegisterUserWithId(const std::string &first_name,
                          const std::string &last_name,
                          const std::string &username,
                          const std::string &password, const int64_t user_id);
  std::vector<int64_t> GetFollowers(int64_t user_id);
  void Unfollow(int64_t user_id, int64_t followee_id);
  void UnfollowWithUsername(const std::string &user_name,
                            const std::string &followee_name);
  void Follow(int64_t user_id, int64_t followee_id);
  void FollowWithUsername(const std::string &user_name,
                          const std::string &followee_name);
  std::vector<int64_t> GetFollowees(int64_t user_id);
  std::vector<Post> ReadHomeTimeline(int64_t user_id, int start, int stop);
  void UploadMedia(const std::string &filename, const std::string &data);
  std::string GetMedia(const std::string &filename);
  void RemovePosts(int64_t user_id, int start, int stop);

private:
  TextServiceReturn ComposeText(const std::string &text);
  std::vector<UserMention>
  ComposeUserMentions(const std::vector<std::string> &usernames);
  std::vector<Url> ComposeUrls(const std::vector<std::string> &urls);
  void WriteUserTimeline(int64_t post_id, int64_t user_id, int64_t timestamp);
  void WriteHomeTimeline(int64_t post_id, int64_t user_id, int64_t timestamp,
                         const std::vector<int64_t> &user_mentions_id);
  std::vector<Post> ReadPosts(const std::vector<int64_t> &post_ids);
  nu::Future<int64_t> GetUserId(const std::string &username);

  RandomStringGenerator random_string_generator_;
  States states_;
};
} // namespace social_network
