#pragma once

#include <iostream>
#include <nu/dis_hash_table.hpp>

#include "../gen-cpp/social_network_types.h"
#include "utils.h"

namespace social_network {

class PostStorageService {
public:
  constexpr static uint32_t kDefaultHashTablePowerNumShards = 9;

  PostStorageService();
  void StorePost(Post post);
  Post ReadPost(int64_t post_id);
  bool RemovePost(int64_t post_id);
  std::vector<Post> ReadPosts(std::vector<int64_t> post_ids);

private:
 nu::DistributedHashTable<int64_t, Post, I64Hasher> _postid_to_post_map;
};

}  // namespace social_network
