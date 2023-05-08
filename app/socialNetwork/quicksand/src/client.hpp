#pragma once

#include <memory>
#include <nu/proclet.hpp>
#include <nu/utils/perf.hpp>
#include <random>

#include "BackEndService.hpp"

namespace social_network {

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

class Client : public nu::PerfAdapter {
 public:
  Client(States states);
  void bench();

 private:
  struct SocialNetPerfThreadState : nu::PerfThreadState {
    SocialNetPerfThreadState(
        const nu::ShardedStatelessService<BackEndService> &s);

    nu::ShardedStatelessService<BackEndService> service;
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

  constexpr static uint32_t kNumThreads = 50;
  constexpr static double kTargetMops = 0.8;
  constexpr static double kTotalMops = 1;
  constexpr static uint32_t kUserTimelinePercent = 60;
  constexpr static uint32_t kHomeTimelinePercent = 30;
  constexpr static uint32_t kComposePostPercent = 5;
  constexpr static uint32_t kRemovePostsPercent = 5;
  constexpr static uint32_t kFollowPercent =
      100 - kUserTimelinePercent - kHomeTimelinePercent - kComposePostPercent -
      kRemovePostsPercent;
  constexpr static uint32_t kNumUsers = 962;
  constexpr static char kCharSet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  constexpr static uint32_t kTextLen = 64;
  constexpr static uint32_t kUrlLen = 64;
  constexpr static uint32_t kMaxNumMentionsPerText = 2;
  constexpr static uint32_t kMaxNumUrlsPerText = 2;
  constexpr static uint32_t kMaxNumMediasPerText = 2;
  constexpr static uint64_t kTimeSeriesIntervalUs = 10 * 1000;
  friend class nu::PerfAdapter;
  nu::ShardedStatelessService<BackEndService> service_;

  std::unique_ptr<nu::PerfThreadState> create_thread_state();
  std::unique_ptr<nu::PerfRequest> gen_req(nu::PerfThreadState *perf_state);
  bool serve_req(nu::PerfThreadState *perf_state,
                 const nu::PerfRequest *perf_req);
  std::unique_ptr<UserTimelineRequest> gen_user_timeline_req(
      SocialNetPerfThreadState *state);
  std::unique_ptr<HomeTimelineRequest> gen_home_timeline_req(
      SocialNetPerfThreadState *state);
  std::unique_ptr<ComposePostRequest> gen_compose_post_req(
      SocialNetPerfThreadState *state);
  std::unique_ptr<RemovePostsRequest> gen_remove_posts_req(
      SocialNetPerfThreadState *state);
  std::unique_ptr<FollowReq> gen_follow_req(SocialNetPerfThreadState *state);
  std::string random_string(uint32_t len, SocialNetPerfThreadState *state);
};

}  // namespace social_network
