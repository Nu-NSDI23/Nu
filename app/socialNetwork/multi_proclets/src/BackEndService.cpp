#include "../gen-cpp/BackEndService.h"

#include <thread.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include <future>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <nu/runtime.hpp>
#include <string>

#include "States.hpp"
#include "utils.h"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;

constexpr static uint32_t kNumEntries = 1;

namespace social_network {

using json = nlohmann::json;

class BackEndHandler : public BackEndServiceIf {
 public:
  BackEndHandler(States states);
  ~BackEndHandler() override = default;

  void RemovePosts(int64_t user_id, int start, int stop);
  void ComposePost(const std::string &username, int64_t user_id,
                   const std::string &text,
                   const std::vector<int64_t> &media_ids,
                   const std::vector<std::string> &media_types,
                   PostType::type post_type) override;
  void ReadUserTimeline(std::vector<Post> &, int64_t, int, int) override;
  void Login(std::string &_return, const std::string &username,
             const std::string &password) override;
  void RegisterUser(const std::string &first_name, const std::string &last_name,
                    const std::string &username,
                    const std::string &password) override;
  void RegisterUserWithId(const std::string &first_name,
                          const std::string &last_name,
                          const std::string &username,
                          const std::string &password,
                          const int64_t user_id) override;
  void GetFollowers(std::vector<int64_t> &_return,
                    const int64_t user_id) override;
  void Unfollow(const int64_t user_id, const int64_t followee_id) override;
  void UnfollowWithUsername(const std::string &user_usernmae,
                            const std::string &followee_username) override;
  void Follow(const int64_t user_id, const int64_t followee_id) override;
  void FollowWithUsername(const std::string &user_usernmae,
                          const std::string &followee_username) override;
  void GetFollowees(std::vector<int64_t> &_return,
                    const int64_t user_id) override;
  void ReadHomeTimeline(std::vector<Post> &_return, const int64_t user_id,
                        const int32_t start, const int32_t stop) override;
  void UploadMedia(const std::string &filename,
                   const std::string &data) override;
  void GetMedia(std::string &_return, const std::string &filename) override;

 private:
  States _states;
};

BackEndHandler::BackEndHandler(States states) : _states(std::move(states)) {}

void BackEndHandler::RemovePosts(int64_t user_id, int start, int stop) {
  auto posts_future = _states.user_timeline_service.run_async(
      &UserTimelineService::ReadUserTimeline, user_id, start, stop);
  auto followers_future = _states.social_graph_service.run_async(
      &SocialGraphService::GetFollowers, user_id);
  auto posts = std::move(posts_future.get());
  auto followers = std::move(followers_future.get());

  std::vector<nu::Future<bool>> remove_post_futures;
  std::vector<nu::Future<void>> remove_from_timeline_futures;
  std::vector<nu::Future<void>> remove_short_url_futures;

  for (auto post : posts) {
    remove_post_futures.emplace_back(_states.post_storage_service.run_async(
        &PostStorageService::RemovePost, post.post_id));

    remove_from_timeline_futures.emplace_back(
        _states.user_timeline_service.run_async(
            &UserTimelineService::RemovePost, user_id, post.post_id,
            post.timestamp));
    for (auto mention : post.user_mentions) {
      remove_from_timeline_futures.emplace_back(
          _states.home_timeline_service.run_async(
              &HomeTimelineService::RemovePost, mention.user_id, post.post_id,
              post.timestamp));
    }
    for (auto user_id : followers) {
      remove_from_timeline_futures.emplace_back(
          _states.home_timeline_service.run_async(
              &HomeTimelineService::RemovePost, user_id, post.post_id,
              post.timestamp));
    }

    std::vector<std::string> shortened_urls;
    for (auto &url : post.urls) {
      shortened_urls.emplace_back(std::move(url.shortened_url));
    }
    remove_short_url_futures.emplace_back(_states.url_shorten_service.run_async(
        &UrlShortenService::RemoveUrls, shortened_urls));
  }
}

void BackEndHandler::ComposePost(const std::string &username, int64_t user_id,
                                 const std::string &text,
                                 const std::vector<int64_t> &media_ids,
                                 const std::vector<std::string> &media_types,
                                 const PostType::type post_type) {
  auto text_service_return_future =
      _states.text_service.run_async(&TextService::ComposeText, text);

  auto unique_id_future = _states.unique_id_service.run_async(
      &UniqueIdService::ComposeUniqueId, post_type);

  auto medias_future = _states.media_service.run_async(
      &MediaService::ComposeMedia, media_types, media_ids);

  auto creator_future = _states.user_service.run_async(
      &UserService::ComposeCreatorWithUserId, user_id, username);

  Post post;
  auto timestamp = rdtsc();
  post.timestamp = timestamp;

  auto unique_id = unique_id_future.get();
  auto write_user_timeline_future = _states.user_timeline_service.run_async(
      &UserTimelineService::WriteUserTimeline, unique_id, user_id, timestamp);

  auto &text_service_return = text_service_return_future.get();
  std::vector<int64_t> user_mention_ids;
  for (auto &item : text_service_return.user_mentions) {
    user_mention_ids.emplace_back(item.user_id);
  }
  auto write_home_timeline_future = _states.home_timeline_service.run_async(
      &HomeTimelineService::WriteHomeTimeline, unique_id, user_id, timestamp,
      user_mention_ids);

  post.text = std::move(text_service_return.text);
  post.urls = std::move(text_service_return.urls);
  post.user_mentions = std::move(text_service_return.user_mentions);
  post.post_id = unique_id;
  post.media = std::move(medias_future.get());
  post.creator = std::move(creator_future.get());
  post.post_type = post_type;

  auto post_future = _states.post_storage_service.run_async(
      &PostStorageService::StorePost, post);

  write_user_timeline_future.get();
  post_future.get();
  write_home_timeline_future.get();
}

void BackEndHandler::ReadUserTimeline(std::vector<Post> &_return,
                                      int64_t user_id, int start, int stop) {
  _return = _states.user_timeline_service.run(
      &UserTimelineService::ReadUserTimeline, user_id, start, stop);
}

void BackEndHandler::Login(std::string &_return, const std::string &username,
                           const std::string &password) {
  auto variant =
      _states.user_service.run(&UserService::Login, username, password);
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
  _return = std::move(std::get<std::string>(variant));
}

void BackEndHandler::RegisterUser(const std::string &first_name,
                                  const std::string &last_name,
                                  const std::string &username,
                                  const std::string &password) {
  _states.user_service.run(&UserService::RegisterUser, first_name, last_name,
                           username, password);
}

void BackEndHandler::RegisterUserWithId(const std::string &first_name,
                                        const std::string &last_name,
                                        const std::string &username,
                                        const std::string &password,
                                        const int64_t user_id) {
  _states.user_service.run(&UserService::RegisterUserWithId, first_name,
                           last_name, username, password, user_id);
}

void BackEndHandler::GetFollowers(std::vector<int64_t> &_return,
                                  const int64_t user_id) {
  _return = _states.social_graph_service.run(&SocialGraphService::GetFollowers,
                                             user_id);
}

void BackEndHandler::Unfollow(const int64_t user_id,
                              const int64_t followee_id) {
  _states.social_graph_service.run(&SocialGraphService::Unfollow, user_id,
                                   followee_id);
}

void BackEndHandler::UnfollowWithUsername(
    const std::string &user_username, const std::string &followee_username) {
  _states.social_graph_service.run(&SocialGraphService::UnfollowWithUsername,
                                   user_username, followee_username);
}

void BackEndHandler::Follow(const int64_t user_id, const int64_t followee_id) {
  _states.social_graph_service.run(&SocialGraphService::Follow, user_id,
                                   followee_id);
}

void BackEndHandler::FollowWithUsername(const std::string &user_username,
                                        const std::string &followee_username) {
  _states.social_graph_service.run(&SocialGraphService::FollowWithUsername,
                                   user_username, followee_username);
}

void BackEndHandler::GetFollowees(std::vector<int64_t> &_return,
                                  const int64_t user_id) {
  _return = _states.social_graph_service.run(&SocialGraphService::GetFollowees,
                                             user_id);
}

void BackEndHandler::ReadHomeTimeline(std::vector<Post> &_return,
                                      const int64_t user_id,
                                      const int32_t start, const int32_t stop) {
  _return = _states.home_timeline_service.run(
      &HomeTimelineService::ReadHomeTimeline, user_id, start, stop);
}

void BackEndHandler::UploadMedia(const std::string &filename,
                                 const std::string &data) {
  _states.media_storage_service.run(&MediaStorageService::UploadMedia, filename,
                                    data);
}

void BackEndHandler::GetMedia(std::string &_return,
                              const std::string &filename) {
  _return = _states.media_storage_service.run(&MediaStorageService::GetMedia,
                                              filename);
}

class ServiceEntry {
 public:
  ServiceEntry(States states) {
    json config_json;
    BUG_ON(load_config_file("config/service-config.json", &config_json) != 0);
    int port = config_json["back-end-service"]["port"];

    std::shared_ptr<TServerSocket> server_socket =
        std::make_shared<TServerSocket>("0.0.0.0", port);

    auto back_end_handler = std::make_shared<BackEndHandler>(std::move(states));

    TThreadedServer server(
        std::make_shared<BackEndServiceProcessor>(std::move(back_end_handler)),
        server_socket, std::make_shared<TFramedTransportFactory>(),
        std::make_shared<TBinaryProtocolFactory>());
    std::cout << "Starting the back-end-service server ..." << std::endl;
    server.serve();
  }
};

}  // namespace social_network

void do_work() {
  auto states = social_network::make_states();

  std::vector<nu::Future<void>> thrift_futures;
  for (uint32_t i = 0; i < kNumEntries; i++) {
    thrift_futures.emplace_back(nu::async([&] {
      nu::make_proclet<social_network::ServiceEntry>(
          std::forward_as_tuple(states), true);
    }));
  }
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
