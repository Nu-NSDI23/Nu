#pragma once

#include <cereal/types/pbds_tree.hpp>
#include <ext/pb_ds/assoc_container.hpp>
#include <iostream>
#include <nu/dis_hash_table.hpp>
#include <nu/proclet.hpp>
#include <string>

#include "PostStorageService.h"
#include "SocialGraphService.h"

namespace social_network {

class HomeTimelineService {
public:
  constexpr static uint32_t kDefaultHashTablePowerNumShards = 9;

  HomeTimelineService(
      nu::Proclet<PostStorageService> post_storage_service,
      nu::Proclet<SocialGraphService> social_graph_service);
  std::vector<Post> ReadHomeTimeline(int64_t, int, int);
  void WriteHomeTimeline(int64_t, int64_t, int64_t, std::vector<int64_t>);
  void RemovePost(int64_t user_id, int64_t post_id, int64_t post_timestamp);

private:
  using Tree =
      __gnu_pbds::tree<std::pair<int64_t, int64_t>, __gnu_pbds::null_type,
                       std::greater<std::pair<int64_t, int64_t>>,
                       __gnu_pbds::rb_tree_tag,
                       __gnu_pbds::tree_order_statistics_node_update>;
  nu::Proclet<PostStorageService> _post_storage_service;
  nu::Proclet<SocialGraphService> _social_graph_service;
  nu::DistributedHashTable<int64_t, Tree, I64Hasher> _userid_to_timeline_map;
};

} // namespace social_network
