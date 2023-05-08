#include "HomeTimelineService.h"

namespace social_network {

HomeTimelineService::HomeTimelineService(
    nu::RemObj<PostStorageService>::Cap post_storage_service_obj_cap,
    nu::RemObj<SocialGraphService>::Cap social_graph_service_obj_cap)
    : _post_storage_service_obj(post_storage_service_obj_cap),
      _social_graph_service_obj(social_graph_service_obj_cap),
      _userid_to_timeline_map(kDefaultHashTablePowerNumShards) {}

void HomeTimelineService::WriteHomeTimeline(
    int64_t post_id, int64_t user_id, int64_t timestamp,
    std::vector<int64_t> user_mentions_id) {
  auto ids =
      _social_graph_service_obj.run(&SocialGraphService::GetFollowers, user_id);
  ids.insert(ids.end(), user_mentions_id.begin(), user_mentions_id.end());

  std::vector<nu::Future<void>> futures;
  for (auto id : ids) {
    futures.emplace_back(_userid_to_timeline_map.apply_async(
        id,
        +[](std::pair<const int64_t, Tree> &p, int64_t timestamp,
            int64_t post_id) {
          (p.second).insert(std::make_pair(timestamp, post_id));
        },
        timestamp, post_id));
  }

  for (auto &future : futures) {
    future.get();
  }
}

std::vector<Post> HomeTimelineService::ReadHomeTimeline(int64_t user_id,
                                                        int start, int stop) {
  if (stop <= start || start < 0) {
    return std::vector<Post>();
  }

  auto post_ids = _userid_to_timeline_map.apply(
      user_id,
      +[](std::pair<const int64_t, Tree> &p, int start, int stop) {
        auto start_iter = p.second.find_by_order(start);
        auto stop_iter = p.second.find_by_order(stop);
        std::vector<int64_t> post_ids;
        for (auto iter = start_iter; iter != stop_iter; iter++) {
          post_ids.push_back(iter->second);
        }
        return post_ids;
      },
      start, stop);
  return _post_storage_service_obj.run(&PostStorageService::ReadPosts,
                                       post_ids);
}

void HomeTimelineService::RemovePost(int64_t user_id, int64_t post_id,
                                     int64_t post_timestamp) {
  _userid_to_timeline_map.apply(
      user_id,
      +[](std::pair<const int64_t, Tree> &p, int64_t timestamp,
          int64_t post_id) {
        (p.second).erase(std::make_pair(timestamp, post_id));
      },
      post_timestamp, post_id);
}

} // namespace social_network
