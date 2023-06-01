#include <atomic>
#include <fstream>
#include <limits>
#include <memory>
#include <nu/commons.hpp>
#include <nu/utils/perf.hpp>
#include <random>
#include <runtime.h>
#include <span>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include "../gen-cpp/BackEndService.h"
#include "../gen-cpp/social_network_types.h"

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

constexpr static uint32_t kNumThreads = 200;
constexpr static double kTargetMops = 2;
constexpr static double kTotalMops = 10;
constexpr static uint32_t kNumEntries = 1;
constexpr static netaddr kClientAddrs[] = {
    {.ip = MAKE_IP_ADDR(18, 18, 1, 253), .port = 9000},
};
constexpr static uint32_t kEntryPort = 9091;
constexpr static uint32_t kUserTimelinePercent = 60;
constexpr static uint32_t kHomeTimelinePercent = 30;
constexpr static uint32_t kComposePostPercent = 5;
constexpr static uint32_t kRemovePostsPercent = 5;
constexpr static uint32_t kFollowPercent =
    100 - kUserTimelinePercent - kHomeTimelinePercent - kComposePostPercent -
    kRemovePostsPercent;
constexpr static uint32_t kNumUsers = 962;
constexpr static char kCharSet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "abcdefghijklmnopqrstuvwxyz";
constexpr static uint32_t kTextLen = 64;
constexpr static uint32_t kUrlLen = 64;
constexpr static uint32_t kMaxNumMentionsPerText = 2;
constexpr static uint32_t kMaxNumUrlsPerText = 2;
constexpr static uint32_t kMaxNumMediasPerText = 2;
constexpr static uint64_t kTimeSeriesIntervalUs = 10 * 1000;

std::string get_entry_ip(int idx) {
  return std::string("18.18.1.") + std::to_string(idx + 2);
}

class ClientPtr {
public:
  ClientPtr(const std::string &ip) {
    socket.reset(new TSocket(ip, kEntryPort));
    transport.reset(new TFramedTransport(socket));
    protocol.reset(new TBinaryProtocol(transport));
    client.reset(new social_network::BackEndServiceClient(protocol));
    transport->open();
  }

  social_network::BackEndServiceClient *operator->() { return client.get(); }

private:
  std::shared_ptr<TTransport> socket;
  std::shared_ptr<TTransport> transport;
  std::shared_ptr<TProtocol> protocol;
  std::unique_ptr<social_network::BackEndServiceClient> client;
};

struct socialNetworkThreadState : nu::PerfThreadState {
  socialNetworkThreadState(const std::string &ip)
      : client(ip), rd(), gen(rd()), dist_1_100(1, 100),
        dist_1_numusers(1, kNumUsers),
        dist_0_charsetsize(0, std::size(kCharSet) - 2),
        dist_0_maxnummentions(0, kMaxNumMentionsPerText),
        dist_0_maxnumurls(0, kMaxNumUrlsPerText),
        dist_0_maxnummedias(0, kMaxNumMediasPerText),
        dist_0_maxint64(0, std::numeric_limits<int64_t>::max()) {}

  ClientPtr client;
  std::random_device rd;
  std::mt19937 gen;
  std::uniform_int_distribution<> dist_1_100;
  std::uniform_int_distribution<> dist_1_numusers;
  std::uniform_int_distribution<> dist_0_charsetsize;
  std::uniform_int_distribution<> dist_0_maxnummentions;
  std::uniform_int_distribution<> dist_0_maxnumurls;
  std::uniform_int_distribution<> dist_0_maxnummedias;
  std::uniform_int_distribution<int64_t> dist_0_maxint64;
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

struct RemovePostsRequest : nu::PerfRequest {
  int64_t user_id;
  int32_t start;
  int32_t stop;
};

struct FollowReq : nu::PerfRequest {
  int64_t user_id;
  int64_t followee_id;
};

class SocialNetworkAdapter : public nu::PerfAdapter {
public:
  std::unique_ptr<nu::PerfThreadState> create_thread_state() {
    static std::atomic<uint32_t> num_threads = 0;
    uint32_t tid = num_threads++;
    return std::make_unique<socialNetworkThreadState>(
        get_entry_ip(tid % kNumEntries));
  }

  std::unique_ptr<nu::PerfRequest> gen_req(nu::PerfThreadState *perf_state) {
    auto *state = reinterpret_cast<socialNetworkThreadState *>(perf_state);

    auto rand_int = (state->dist_1_100)(state->gen);
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
    rand_int -= kComposePostPercent;
    if (rand_int <= kRemovePostsPercent) {
      return gen_remove_posts_req(state);
    }
    return gen_follow_req(state);
  }

  bool serve_req(nu::PerfThreadState *perf_state, const nu::PerfRequest *perf_req) {
    try {
      auto *state = reinterpret_cast<socialNetworkThreadState *>(perf_state);
      auto *user_timeline_req =
          dynamic_cast<const UserTimelineRequest *>(perf_req);
      if (user_timeline_req) {
        std::vector<social_network::Post> unused;
        state->client->ReadUserTimeline(unused, user_timeline_req->user_id,
                                        user_timeline_req->start,
                                        user_timeline_req->stop);
        return true;
      }
      auto *home_timeline_req =
          dynamic_cast<const HomeTimelineRequest *>(perf_req);
      if (home_timeline_req) {
        std::vector<social_network::Post> unused;
        state->client->ReadHomeTimeline(unused, home_timeline_req->user_id,
                                        home_timeline_req->start,
                                        home_timeline_req->stop);
        return true;
      }
      auto *compose_post_req =
          dynamic_cast<const ComposePostRequest *>(perf_req);
      if (compose_post_req) {
        state->client->ComposePost(
            compose_post_req->username, compose_post_req->user_id,
            compose_post_req->text, compose_post_req->media_ids,
            compose_post_req->media_types, compose_post_req->post_type);
        return true;
      }
      auto *remove_posts_req =
          dynamic_cast<const RemovePostsRequest *>(perf_req);
      if (remove_posts_req) {
        state->client->RemovePosts(remove_posts_req->user_id,
                                   remove_posts_req->start,
                                   remove_posts_req->stop);
        return true;
      }
      auto *follow_req = dynamic_cast<const FollowReq *>(perf_req);
      if (follow_req) {
        state->client->Follow(follow_req->user_id, follow_req->followee_id);
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
    user_timeline_req->user_id = (state->dist_1_numusers)(state->gen);
    user_timeline_req->start = (state->dist_1_100)(state->gen);
    user_timeline_req->stop = user_timeline_req->start + 1;
    return std::unique_ptr<UserTimelineRequest>(user_timeline_req);
  }

  std::unique_ptr<HomeTimelineRequest>
  gen_home_timeline_req(socialNetworkThreadState *state) {
    auto *home_timeline_req = new HomeTimelineRequest();
    home_timeline_req->user_id = (state->dist_1_numusers)(state->gen);
    home_timeline_req->start = (state->dist_1_100)(state->gen);
    home_timeline_req->stop = home_timeline_req->start + 1;
    return std::unique_ptr<HomeTimelineRequest>(home_timeline_req);
  }

  std::unique_ptr<ComposePostRequest>
  gen_compose_post_req(socialNetworkThreadState *state) {
    auto *compose_post_req = new ComposePostRequest();
    compose_post_req->user_id = (state->dist_1_numusers)(state->gen);
    compose_post_req->username =
        std::string("username_") + std::to_string(compose_post_req->user_id);
    compose_post_req->text = random_string(kTextLen, state);
    auto num_user_mentions = (state->dist_0_maxnummentions)(state->gen);
    for (uint32_t i = 0; i < num_user_mentions; i++) {
      auto mentioned_id = (state->dist_1_numusers)(state->gen);
      compose_post_req->text += " @username_" + std::to_string(mentioned_id);
    }
    auto num_urls = (state->dist_0_maxnumurls)(state->gen);
    for (uint32_t i = 0; i < num_urls; i++) {
      compose_post_req->text += " http://" + random_string(kUrlLen, state);
    }
    auto num_medias = (state->dist_0_maxnummedias)(state->gen);
    for (uint32_t i = 0; i < num_medias; i++) {
      compose_post_req->media_ids.emplace_back(
          (state->dist_0_maxint64)(state->gen));
      compose_post_req->media_types.push_back("png");
    }
    compose_post_req->post_type = social_network::PostType::POST;
    return std::unique_ptr<ComposePostRequest>(compose_post_req);
  }

  std::unique_ptr<RemovePostsRequest>
  gen_remove_posts_req(socialNetworkThreadState *state) {
    auto *remove_posts_req = new RemovePostsRequest();
    remove_posts_req->user_id = (state->dist_1_numusers)(state->gen);
    remove_posts_req->start = 0;
    remove_posts_req->stop = remove_posts_req->start + 1;
    return std::unique_ptr<RemovePostsRequest>(remove_posts_req);
  }

  std::unique_ptr<FollowReq> gen_follow_req(socialNetworkThreadState *state) {
    auto *follow_req = new FollowReq();
    follow_req->user_id = (state->dist_1_numusers)(state->gen);
    follow_req->followee_id = (state->dist_1_numusers)(state->gen);
    return std::unique_ptr<FollowReq>(follow_req);
  }

  std::string random_string(uint32_t len, socialNetworkThreadState *state) {
    std::string str = "";
    for (uint32_t i = 0; i < kTextLen; i++) {
      str += kCharSet[(state->dist_0_charsetsize)(state->gen)];
    }
    return str;
  }
};

void do_work() {
  SocialNetworkAdapter social_network_adapter;
  nu::Perf perf(social_network_adapter);
  auto duration_us = kTotalMops / kTargetMops * 1000 * 1000;
  auto warmup_us = duration_us;
  perf.run_multi_clients(std::span(kClientAddrs), kNumThreads,
                         kTargetMops / std::size(kClientAddrs), duration_us,
                         warmup_us, 10 * nu::kOneMilliSecond);
  std::cout << "real_mops, avg_lat, 50th_lat, 90th_lat, 95th_lat, 99th_lat, "
               "99.9th_lat"
            << std::endl;
  std::cout << perf.get_real_mops() << " " << perf.get_average_lat() << " "
            << perf.get_nth_lat(50) << " " << perf.get_nth_lat(90) << " "
            << perf.get_nth_lat(95) << " " << perf.get_nth_lat(99) << " "
            << perf.get_nth_lat(99.9) << std::endl;
  auto timeseries_vec = perf.get_timeseries_nth_lats(kTimeSeriesIntervalUs, 99);
  std::ofstream ofs("timeseries");
  for (auto [_, us, lat] : timeseries_vec) {
    ofs << us << " " << lat << std::endl;
  }
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
