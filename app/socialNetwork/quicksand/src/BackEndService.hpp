#pragma once

#include <cereal/archives/binary.hpp>
#include <cereal/types/set.hpp>
#include <string>
#include <variant>
#include <vector>

#include "defs.hpp"
#include "states.hpp"
#include "utils.hpp"

namespace social_network {

class BackEndService {
 public:
  BackEndService() = default;
  BackEndService(States states);
  BackEndService(const BackEndService &backend);
  BackEndService &operator=(const BackEndService &);
  void ComposePost(std::string username, int64_t user_id, std::string text,
                   std::vector<int64_t> media_ids,
                   std::vector<std::string> media_types,
                   PostType::type post_type);
  std::vector<Post> ReadUserTimeline(int64_t user_id, int start, int stop);
  std::variant<LoginErrorCode, std::string> Login(std::string username,
                                                  std::string password);
  void RegisterUser(std::string first_name, std::string last_name,
                    std::string username, std::string password);
  void RegisterUserWithId(std::string first_name, std::string last_name,
                          std::string username, std::string password,
                          int64_t user_id);
  std::vector<int64_t> GetFollowers(int64_t user_id);
  void Unfollow(int64_t user_id, int64_t followee_id);
  void UnfollowWithUsername(std::string user_name, std::string followee_name);
  void Follow(int64_t user_id, int64_t followee_id);
  void FollowWithUsername(std::string user_name, std::string followee_name);
  std::vector<int64_t> GetFollowees(int64_t user_id);
  std::vector<Post> ReadHomeTimeline(int64_t user_id, int start, int stop);
  void UploadMedia(std::string filename, std::string data);
  std::string GetMedia(std::string filename);
  void RemovePosts(int64_t user_id, int start, int stop);

  // boilerplate...
  using Key = uint64_t;
  bool empty() const { return false; }
  std::size_t size() const { return 1; }
  void split(Key *, BackEndService *) {}
  template <class Archive>
  void serialize(Archive &ar) {
    ar(states_);
  }

 private:
  TextServiceReturn ComposeText(const std::string &text);
  std::vector<UserMention> ComposeUserMentions(
      const std::vector<std::string> &usernames);
  std::vector<Url> ComposeUrls(const std::vector<std::string> &urls);
  void WriteUserTimeline(int64_t post_id, int64_t user_id, int64_t timestamp);
  void WriteHomeTimeline(int64_t post_id, int64_t user_id, int64_t timestamp,
                         const std::vector<int64_t> &user_mentions_id);
  std::vector<Post> ReadPosts(const std::vector<int64_t> &post_ids);
  nu::Future<int64_t> GetUserId(const std::string &username);

  RandomStringGenerator random_string_generator_;
  States states_;
};
}  // namespace social_network
