#include "PostStorageService.h"

namespace social_network {

PostStorageService::PostStorageService()
    : _postid_to_post_map(nu::make_dis_hash_table<int64_t, Post, I64Hasher>(
          kDefaultHashTablePowerNumShards)) {}

void PostStorageService::StorePost(social_network::Post post) {
  _postid_to_post_map.put(post.post_id, std::move(post));
}

bool PostStorageService::RemovePost(int64_t post_id) {
  return _postid_to_post_map.remove(post_id);
}

Post PostStorageService::ReadPost(int64_t post_id) {
  auto optional = _postid_to_post_map.get(post_id);
  BUG_ON(!optional);
  return *optional;
}

std::vector<Post> PostStorageService::ReadPosts(std::vector<int64_t> post_ids) {
  std::vector<nu::Future<std::optional<Post>>> post_futures;
  for (auto post_id : post_ids) {
    post_futures.emplace_back(_postid_to_post_map.get_async(post_id));
  }
  std::vector<Post> posts;
  for (auto &post_future : post_futures) {
    auto &optional = post_future.get();
    if (optional) {
      posts.emplace_back(std::move(*optional));
    }
  }
  return posts;
}

} // namespace social_network
