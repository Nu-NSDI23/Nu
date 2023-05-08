#pragma once

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>

#include "BackEndService.hpp"

using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;

namespace social_network {

class ThriftBackEndServer : public BackEndServiceIf {
public:
 ThriftBackEndServer(States &&states);
 void ComposePost(const std::string &username, int64_t user_id,
                  const std::string &text,
                  const std::vector<int64_t> &media_ids,
                  const std::vector<std::string> &media_types,
                  PostType::type post_type) override;
 void ReadUserTimeline(std::vector<Post> &, int64_t, int, int) override;
 void Login(std::string &ret, const std::string &username,
            const std::string &password) override;
 void RegisterUser(const std::string &first_name, const std::string &last_name,
                   const std::string &username,
                   const std::string &password) override;
 void RegisterUserWithId(const std::string &first_name,
                         const std::string &last_name,
                         const std::string &username,
                         const std::string &password,
                         const int64_t user_id) override;
 void GetFollowers(std::vector<int64_t> &ret, const int64_t user_id) override;
 void Unfollow(const int64_t user_id, const int64_t followee_id) override;
 void UnfollowWithUsername(const std::string &user_usernmae,
                           const std::string &followee_username) override;
 void Follow(const int64_t user_id, const int64_t followee_id) override;
 void FollowWithUsername(const std::string &user_usernmae,
                         const std::string &followee_username) override;
 void GetFollowees(std::vector<int64_t> &ret, const int64_t user_id) override;
 void ReadHomeTimeline(std::vector<Post> &ret, const int64_t user_id,
                       const int32_t start, const int32_t stop) override;
 void UploadMedia(const std::string &filename,
                  const std::string &data) override;
 void GetMedia(std::string &ret, const std::string &filename) override;
 void RemovePosts(int64_t user_id, int32_t start, int32_t stop) override;
 void NoOp() override;

private:
  BackEndService back_end_service_;
};
} // namespace social_network
