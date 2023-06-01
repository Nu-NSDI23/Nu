#include "ThriftBackEndServer.hpp"

namespace social_network {

ThriftBackEndServer::ThriftBackEndServer(States &&states)
    : back_end_service_(std::move(states)) {}

void ThriftBackEndServer::ComposePost(
    const std::string &username, int64_t user_id, const std::string &text,
    const std::vector<int64_t> &media_ids,
    const std::vector<std::string> &media_types,
    const PostType::type post_type) {
  back_end_service_.ComposePost(username, user_id, text, media_ids, media_types,
                                post_type);
}

void ThriftBackEndServer::ReadUserTimeline(std::vector<Post> &ret,
                                           int64_t user_id, int start,
                                           int stop) {
  ret = back_end_service_.ReadUserTimeline(user_id, start, stop);
}

void ThriftBackEndServer::Login(std::string &ret, const std::string &username,
                                const std::string &password) {
  auto variant = back_end_service_.Login(username, password);
  if (std::holds_alternative<LoginErrorCode>(variant)) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_UNAUTHORIZED;
    auto &login_error_code = std::get<LoginErrorCode>(variant);
    switch (login_error_code) {
    case NOT_REGISTERED:
      se.message = "The username is not registered yet.";
      break;
    case WRONG_PASSWORD:
      se.message = "Wrong password.";
      break;
    default:
      break;
    }
    throw se;
  }
  ret = std::get<std::string>(variant);
}

void ThriftBackEndServer::RegisterUser(const std::string &first_name,
                                       const std::string &last_name,
                                       const std::string &username,
                                       const std::string &password) {
  back_end_service_.RegisterUser(first_name, last_name, username, password);
}

void ThriftBackEndServer::RegisterUserWithId(const std::string &first_name,
                                             const std::string &last_name,
                                             const std::string &username,
                                             const std::string &password,
                                             const int64_t user_id) {
  back_end_service_.RegisterUserWithId(first_name, last_name, username,
                                       password, user_id);
}

void ThriftBackEndServer::GetFollowers(std::vector<int64_t> &ret,
                                       const int64_t user_id) {
  ret = back_end_service_.GetFollowers(user_id);
}

void ThriftBackEndServer::Unfollow(const int64_t user_id,
                                   const int64_t followee_id) {
  back_end_service_.Unfollow(user_id, followee_id);
}

void ThriftBackEndServer::UnfollowWithUsername(
    const std::string &user_username, const std::string &followee_username) {
  back_end_service_.UnfollowWithUsername(user_username, followee_username);
}

void ThriftBackEndServer::Follow(const int64_t user_id,
                                 const int64_t followee_id) {
  back_end_service_.Follow(user_id, followee_id);
}

void ThriftBackEndServer::FollowWithUsername(
    const std::string &user_username, const std::string &followee_username) {
  back_end_service_.FollowWithUsername(user_username, followee_username);
}

void ThriftBackEndServer::GetFollowees(std::vector<int64_t> &ret,
                                       const int64_t user_id) {
  ret = back_end_service_.GetFollowees(user_id);
}

void ThriftBackEndServer::ReadHomeTimeline(std::vector<Post> &ret,
                                           const int64_t user_id,
                                           const int32_t start,
                                           const int32_t stop) {
  ret = back_end_service_.ReadHomeTimeline(user_id, start, stop);
}

void ThriftBackEndServer::UploadMedia(const std::string &filename,
                                      const std::string &data) {
  back_end_service_.UploadMedia(filename, data);
}

void ThriftBackEndServer::GetMedia(std::string &ret,
                                   const std::string &filename) {
  ret = back_end_service_.GetMedia(filename);
}

void ThriftBackEndServer::RemovePosts(int64_t user_id, int32_t start,
                                      int32_t stop) {
  back_end_service_.RemovePosts(user_id, start, stop);
}

void ThriftBackEndServer::NoOp() {}

} // namespace social_network
