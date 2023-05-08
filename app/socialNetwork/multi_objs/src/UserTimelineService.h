#pragma once

#include <algorithm>
#include <cereal/types/pbds_tree.hpp>
#include <ext/pb_ds/assoc_container.hpp>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <nu/dis_hash_table.hpp>
#include <nu/rem_obj.hpp>
#include <string>
#include <utility>

#include "PostStorageService.h"

namespace social_network {

class UserTimelineService {
public:
  constexpr static uint32_t kDefaultHashTablePowerNumShards = 9;

  UserTimelineService(nu::RemObj<PostStorageService>::Cap cap);
  void WriteUserTimeline(int64_t post_id, int64_t user_id, int64_t timestamp);
  std::vector<Post> ReadUserTimeline(int64_t, int, int);
  void RemovePost(int64_t user_id, int64_t post_id, int64_t post_timestamp);

private:
  using Tree =
      __gnu_pbds::tree<std::pair<int64_t, int64_t>, __gnu_pbds::null_type,
                       std::greater<std::pair<int64_t, int64_t>>,
                       __gnu_pbds::rb_tree_tag,
                       __gnu_pbds::tree_order_statistics_node_update>;
  nu::RemObj<PostStorageService> _post_storage_service_obj;
  nu::DistributedHashTable<int64_t, Tree, decltype(kHashI64toU64)>
      _userid_to_timeline_map;
};

}  // namespace social_network
