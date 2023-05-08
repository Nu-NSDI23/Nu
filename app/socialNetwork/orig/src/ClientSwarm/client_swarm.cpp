#include <iostream>
#include <limits>
#include <memory>
#include <nu/commons.hpp>
#include <nu/utils/perf.hpp>
#include <random>
#include <runtime.h>
#include <string>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include "../../gen-cpp/UserTimelineService.h"
#include "../../gen-cpp/HomeTimelineService.h"
#include "../../gen-cpp/ComposePostService.h"
#include "../../gen-cpp/SocialGraphService.h"
#include "../../gen-cpp/social_network_types.h"

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

constexpr static uint32_t kNumThreads = 200;
constexpr static double kTargetMops = 0.04;
constexpr static double kTotalMops = 1.0;
constexpr static char kUserTimeLineIP[] = "10.10.1.6";
constexpr static uint32_t kUserTimeLinePort = 10003;
constexpr static char kHomeTimeLineIP[] = "10.10.1.6";
constexpr static uint32_t kHomeTimeLinePort = 10010;
constexpr static char kComposePostIP[] = "10.10.1.4";
constexpr static uint32_t kComposePostPort = 10001;
constexpr static char kSocialGraphIP[] = "10.10.1.2";
constexpr static uint32_t kSocialGraphPort = 10000;
constexpr static uint32_t kUserTimelinePercent = 60;
constexpr static uint32_t kHomeTimelinePercent = 30;
constexpr static uint32_t kComposePostPercent = 10;
constexpr static uint32_t kFollowPercent =
    100 - kUserTimelinePercent - kHomeTimelinePercent - kComposePostPercent;
constexpr static uint32_t kNumUsers = 962;
constexpr static char kCharSet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
constexpr static uint32_t kTextLen = 64;
constexpr static uint32_t kUrlLen = 64;
constexpr static uint32_t kMaxNumMentionsPerText = 2;
constexpr static uint32_t kMaxNumUrlsPerText = 2;
constexpr static uint32_t kMaxNumMediasPerText = 2;

template <class Service>
class WrappedClient  {
public:
  void init(std::string ip, uint32_t port) {
    socket_.reset(new TSocket(ip.c_str(), port));
    transport_.reset(new TFramedTransport(socket_));
    protocol_.reset(new TBinaryProtocol(transport_));
    client_.reset(new Service(protocol_));
    transport_->open();
  }
  Service *get() { return client_.get(); }

private:
  std::shared_ptr<TTransport> socket_;
  std::shared_ptr<TTransport> transport_;
  std::shared_ptr<TProtocol> protocol_;
  std::unique_ptr<Service> client_;
};

struct socialNetworkThreadState : nu::PerfThreadState {
  WrappedClient<social_network::UserTimelineServiceClient> user_timeline_client;
  WrappedClient<social_network::HomeTimelineServiceClient> home_timeline_client;
  WrappedClient<social_network::ComposePostServiceClient> compose_post_client;
  WrappedClient<social_network::SocialGraphServiceClient> social_graph_client;
  std::random_device rd;
  std::unique_ptr<std::mt19937> gen;
  std::unique_ptr<std::uniform_int_distribution<>> dist_1_100;
  std::unique_ptr<std::uniform_int_distribution<>> dist_1_numusers;
  std::unique_ptr<std::uniform_int_distribution<>> dist_0_charsetsize;
  std::unique_ptr<std::uniform_int_distribution<>> dist_0_maxnummentions;
  std::unique_ptr<std::uniform_int_distribution<>> dist_0_maxnumurls;
  std::unique_ptr<std::uniform_int_distribution<>> dist_0_maxnummedias;
  std::unique_ptr<std::uniform_int_distribution<int64_t>> dist_0_maxint64;
};

struct UserTimelineRequest : nu::PerfRequest {
  int64_t user_id;
  int32_t start;
  int32_t stop;
};

struct HomeTimelineRequest : nu::PerfRequest {
  int64_t user_id;
  int32_t start;
  int32_t stop;
};

struct ComposePostRequest : nu::PerfRequest {
  std::string username;
  int64_t user_id;
  std::string text;
  std::vector<int64_t> media_ids;
  std::vector<std::string> media_types;
  social_network::PostType::type post_type;
};

struct FollowReq : nu::PerfRequest {
  int64_t user_id;
  int64_t followee_id;
};

class SocialNetworkAdapter : public nu::PerfAdapter {
public:
  std::unique_ptr<nu::PerfThreadState> create_thread_state() {
    auto state = new socialNetworkThreadState();
    state->user_timeline_client.init(std::string(kUserTimeLineIP),
                                     kUserTimeLinePort);
    state->home_timeline_client.init(std::string(kHomeTimeLineIP),
                                     kHomeTimeLinePort);
    state->compose_post_client.init(std::string(kComposePostIP),
                                    kComposePostPort);
    state->social_graph_client.init(std::string(kSocialGraphIP),
                                    kSocialGraphPort);
    state->gen.reset(new std::mt19937((state->rd)()));
    state->dist_1_100.reset(new std::uniform_int_distribution<>(1, 100));
    state->dist_1_numusers.reset(
        new std::uniform_int_distribution<>(1, kNumUsers));
    state->dist_0_charsetsize.reset(
        new std::uniform_int_distribution<>(0, sizeof(kCharSet) - 2));
    state->dist_0_maxnummentions.reset(
        new std::uniform_int_distribution<>(0, kMaxNumMentionsPerText));
    state->dist_0_maxnumurls.reset(
        new std::uniform_int_distribution<>(0, kMaxNumUrlsPerText));
    state->dist_0_maxnummedias.reset(
        new std::uniform_int_distribution<>(0, kMaxNumMediasPerText));
    state->dist_0_maxint64.reset(new std::uniform_int_distribution<int64_t>(
        0, std::numeric_limits<int64_t>::max()));
    return std::unique_ptr<nu::PerfThreadState>(state);
  }

  std::unique_ptr<nu::PerfRequest> gen_req(nu::PerfThreadState *perf_state) {
    auto *state = reinterpret_cast<socialNetworkThreadState *>(perf_state);
    auto rand_int = (*state->dist_1_100)(*state->gen);
    if (rand_int <= kUserTimelinePercent) {
      return gen_user_timeline_req(state);
    }
    rand_int -= kUserTimelinePercent;
    if (rand_int <= kHomeTimelinePercent) {
      return gen_home_timeline_req(state);
    }
    rand_int -= kHomeTimelinePercent;
    if (rand_int <= kComposePostPercent) {
      return gen_compose_post_req(state);
    }
    return gen_follow_req(state);
  }

  bool serve_req(nu::PerfThreadState *perf_state,
                 const nu::PerfRequest *perf_req) {
    try {
      auto *state = reinterpret_cast<socialNetworkThreadState *>(perf_state);
      auto *user_timeline_req =
          dynamic_cast<const UserTimelineRequest *>(perf_req);
      int64_t req_id = microtime();
      if (user_timeline_req) {
        std::vector<social_network::Post> ret;
        std::map<std::string, std::string> carrier;
        state->user_timeline_client.get()->ReadUserTimeline(
            ret, req_id, user_timeline_req->user_id, user_timeline_req->start,
            user_timeline_req->stop, carrier);
        return true;
      }
      auto *home_timeline_req =
          dynamic_cast<const HomeTimelineRequest *>(perf_req);
      if (home_timeline_req) {
        std::vector<social_network::Post> ret;
        std::map<std::string, std::string> carrier;
        state->home_timeline_client.get()->ReadHomeTimeline(
            ret, req_id, home_timeline_req->user_id, home_timeline_req->start,
            home_timeline_req->stop, carrier);
        return true;
      }
      auto *compose_post_req =
          dynamic_cast<const ComposePostRequest *>(perf_req);
      if (compose_post_req) {
        std::map<std::string, std::string> carrier;
        state->compose_post_client.get()->ComposePost(
            req_id, compose_post_req->username, compose_post_req->user_id,
            compose_post_req->text, compose_post_req->media_ids,
            compose_post_req->media_types, compose_post_req->post_type,
            carrier);
        return true;
      }
      auto *follow_req = dynamic_cast<const FollowReq *>(perf_req);
      if (follow_req) {
        std::map<std::string, std::string> carrier;
        state->social_graph_client.get()->Follow(
            req_id, follow_req->user_id, follow_req->followee_id, carrier);
        return true;
      }
    } catch (...) {
      return false;
    }
    return true;
  }

private:
  std::unique_ptr<UserTimelineRequest>
  gen_user_timeline_req(socialNetworkThreadState *state) {
    auto *user_timeline_req = new UserTimelineRequest();
    user_timeline_req->user_id = (*state->dist_1_numusers)(*state->gen);
    user_timeline_req->start = (*state->dist_1_100)(*state->gen);
    user_timeline_req->stop = user_timeline_req->start + 1;
    return std::unique_ptr<UserTimelineRequest>(user_timeline_req);
  }

  std::unique_ptr<HomeTimelineRequest>
  gen_home_timeline_req(socialNetworkThreadState *state) {
    auto *home_timeline_req = new HomeTimelineRequest();
    home_timeline_req->user_id = (*state->dist_1_numusers)(*state->gen);
    home_timeline_req->start = (*state->dist_1_100)(*state->gen);
    home_timeline_req->stop = home_timeline_req->start + 1;
    return std::unique_ptr<HomeTimelineRequest>(home_timeline_req);
  }

  std::unique_ptr<ComposePostRequest>
  gen_compose_post_req(socialNetworkThreadState *state) {
    auto *compose_post_req = new ComposePostRequest();
    compose_post_req->user_id = (*state->dist_1_numusers)(*state->gen);
    compose_post_req->username =
        std::string("username_") + std::to_string(compose_post_req->user_id);
    compose_post_req->text = random_string(kTextLen, state);
    auto num_user_mentions = (*state->dist_0_maxnummentions)(*state->gen);
    for (uint32_t i = 0; i < num_user_mentions; i++) {
      auto mentioned_id = (*state->dist_1_numusers)(*state->gen);
      compose_post_req->text += " @username_" + std::to_string(mentioned_id);
    }
    auto num_urls = (*state->dist_0_maxnumurls)(*state->gen);
    for (uint32_t i = 0; i < num_urls; i++) {
      compose_post_req->text += " http://" + random_string(kUrlLen, state);
    }
    auto num_medias = (*state->dist_0_maxnummedias)(*state->gen);
    for (uint32_t i = 0; i < num_medias; i++) {
      compose_post_req->media_ids.emplace_back(
          (*state->dist_0_maxint64)(*state->gen));
      compose_post_req->media_types.push_back("png");
    }
    compose_post_req->post_type = social_network::PostType::POST;
    return std::unique_ptr<ComposePostRequest>(compose_post_req);
  }

  std::unique_ptr<FollowReq> gen_follow_req(socialNetworkThreadState *state) {
    auto *follow_req = new FollowReq();
    follow_req->user_id = (*state->dist_1_numusers)(*state->gen);
    follow_req->followee_id = (*state->dist_1_numusers)(*state->gen);
    return std::unique_ptr<FollowReq>(follow_req);
  }

  std::string random_string(uint32_t len, socialNetworkThreadState *state) {
    std::string str = "";
    for (uint32_t i = 0; i < kTextLen; i++) {
      auto idx = (*state->dist_0_charsetsize)(*state->gen);
      str += kCharSet[idx];
    }
    return str;
  }
};

void do_work() {
  SocialNetworkAdapter social_network_adapter;
  nu::Perf perf(social_network_adapter);
  auto duration_us = kTotalMops / kTargetMops * 1000 * 1000;
  auto warmup_us = duration_us;
  perf.run(kNumThreads, kTargetMops, duration_us, warmup_us, nu::kOneSecond);
  std::cout << "real_mops, avg_lat, 50th_lat, 90th_lat, 95th_lat, 99th_lat, "
               "99.9th_lat"
            << std::endl;
  std::cout << perf.get_real_mops() << " " << perf.get_average_lat() << " "
            << perf.get_nth_lat(50) << " " << perf.get_nth_lat(90) << " "
            << perf.get_nth_lat(95) << " " << perf.get_nth_lat(99) << " "
            << perf.get_nth_lat(99.9) << std::endl;
}

int main(int argc, char **argv) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = rt::RuntimeInit(std::string(argv[1]), [] { do_work(); });

  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
