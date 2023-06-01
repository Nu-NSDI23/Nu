#include <cereal/types/vector.hpp>

#include "UserTimelineService.h"
#include "../gen-cpp/social_network_types.h"

namespace social_network {

UserTimelineService::UserTimelineService(
    nu::Proclet<PostStorageService> proclet)
    : _post_storage_service(std::move(proclet)),
      _userid_to_timeline_map(nu::make_dis_hash_table<int64_t, Tree, I64Hasher>(
          kDefaultHashTablePowerNumShards)) {}

void UserTimelineService::WriteUserTimeline(int64_t post_id, int64_t user_id,
                                            int64_t timestamp) {
  _userid_to_timeline_map.apply(
      user_id,
      +[](std::pair<const int64_t, Tree> &p, int64_t timestamp,
          int64_t post_id) {
        (p.second).insert(std::make_pair(timestamp, post_id));
      },
      timestamp, post_id);
}

std::vector<Post> UserTimelineService::ReadUserTimeline(int64_t user_id,
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
  return _post_storage_service.run(&PostStorageService::ReadPosts, post_ids);
}

void UserTimelineService::RemovePost(int64_t user_id, int64_t post_id,
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
