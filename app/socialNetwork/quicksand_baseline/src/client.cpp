#include <fstream>

#include "client.hpp"

namespace social_network {

Client::SocialNetPerfThreadState::SocialNetPerfThreadState()
    : rd(),
      gen(rd()),
      dist_1_100(1, 100),
      dist_1_numusers(1, kNumUsers),
      dist_0_charsetsize(0, std::size(kCharSet) - 2),
      dist_0_maxnummentions(0, kMaxNumMentionsPerText),
      dist_0_maxnumurls(0, kMaxNumUrlsPerText),
      dist_0_maxnummedias(0, kMaxNumMediasPerText),
      dist_0_maxint64(0, std::numeric_limits<int64_t>::max()) {}

Client::Client(nu::Proclet<BackEndService> backend)
    : backend_(std::move(backend)) {}

void Client::bench() {
  std::cout << "benching..." << std::endl;

  nu::Perf perf(*this);
  auto duration_us = kTotalMops / kTargetMops * 1000 * 1000;
  auto warmup_us = duration_us;
  perf.run(kNumThreads, kTargetMops, duration_us, warmup_us,
           50 * nu::kOneMilliSecond);
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

std::unique_ptr<nu::PerfThreadState> Client::create_thread_state() {
  return std::make_unique<SocialNetPerfThreadState>();
}

std::unique_ptr<nu::PerfRequest> Client::gen_req(
    nu::PerfThreadState *perf_state) {
  auto *state = reinterpret_cast<SocialNetPerfThreadState *>(perf_state);

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

bool Client::serve_req(nu::PerfThreadState *perf_state,
                       const nu::PerfRequest *perf_req) {
  try {
    auto *state = reinterpret_cast<SocialNetPerfThreadState *>(perf_state);
    auto *user_timeline_req =
        dynamic_cast<const UserTimelineRequest *>(perf_req);
    if (user_timeline_req) {
      backend_.run(&BackEndService::ReadUserTimeline,
                   user_timeline_req->user_id, user_timeline_req->start,
                   user_timeline_req->stop);
      return true;
    }
    auto *home_timeline_req =
        dynamic_cast<const HomeTimelineRequest *>(perf_req);
    if (home_timeline_req) {
      backend_.run(&BackEndService::ReadHomeTimeline,
                   home_timeline_req->user_id, home_timeline_req->start,
                   home_timeline_req->stop);
      return true;
    }
    auto *compose_post_req = dynamic_cast<const ComposePostRequest *>(perf_req);
    if (compose_post_req) {
      backend_.run(&BackEndService::ComposePost, compose_post_req->username,
                   compose_post_req->user_id, compose_post_req->text,
                   compose_post_req->media_ids, compose_post_req->media_types,
                   compose_post_req->post_type);
      return true;
    }
    auto *remove_posts_req = dynamic_cast<const RemovePostsRequest *>(perf_req);
    if (remove_posts_req) {
      backend_.run(&BackEndService::RemovePosts, remove_posts_req->user_id,
                   remove_posts_req->start, remove_posts_req->stop);
      return true;
    }
    auto *follow_req = dynamic_cast<const FollowReq *>(perf_req);
    if (follow_req) {
      backend_.run(&BackEndService::Follow, follow_req->user_id,
                   follow_req->followee_id);
      return true;
    }
  } catch (...) {
    return false;
  }
  return true;
}

std::unique_ptr<UserTimelineRequest> Client::gen_user_timeline_req(
    SocialNetPerfThreadState *state) {
  auto *user_timeline_req = new UserTimelineRequest();
  user_timeline_req->user_id = (state->dist_1_numusers)(state->gen);
  user_timeline_req->start = (state->dist_1_100)(state->gen);
  user_timeline_req->stop = user_timeline_req->start + 1;
  return std::unique_ptr<UserTimelineRequest>(user_timeline_req);
}

std::unique_ptr<HomeTimelineRequest> Client::gen_home_timeline_req(
    SocialNetPerfThreadState *state) {
  auto *home_timeline_req = new HomeTimelineRequest();
  home_timeline_req->user_id = (state->dist_1_numusers)(state->gen);
  home_timeline_req->start = (state->dist_1_100)(state->gen);
  home_timeline_req->stop = home_timeline_req->start + 1;
  return std::unique_ptr<HomeTimelineRequest>(home_timeline_req);
}

std::unique_ptr<ComposePostRequest> Client::gen_compose_post_req(
    SocialNetPerfThreadState *state) {
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

std::unique_ptr<RemovePostsRequest> Client::gen_remove_posts_req(
    SocialNetPerfThreadState *state) {
  auto *remove_posts_req = new RemovePostsRequest();
  remove_posts_req->user_id = (state->dist_1_numusers)(state->gen);
  remove_posts_req->start = 0;
  remove_posts_req->stop = remove_posts_req->start + 1;
  return std::unique_ptr<RemovePostsRequest>(remove_posts_req);
}

std::unique_ptr<FollowReq> Client::gen_follow_req(
    SocialNetPerfThreadState *state) {
  auto *follow_req = new FollowReq();
  follow_req->user_id = (state->dist_1_numusers)(state->gen);
  follow_req->followee_id = (state->dist_1_numusers)(state->gen);
  return std::unique_ptr<FollowReq>(follow_req);
}

std::string Client::random_string(uint32_t len,
                                  SocialNetPerfThreadState *state) {
  std::string str = "";
  for (uint32_t i = 0; i < kTextLen; i++) {
    str += kCharSet[(state->dist_0_charsetsize)(state->gen)];
  }
  return str;
}

}  // namespace social_network
