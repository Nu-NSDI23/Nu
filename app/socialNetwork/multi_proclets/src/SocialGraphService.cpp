#include "SocialGraphService.h"

namespace social_network {

SocialGraphService::SocialGraphService(nu::Proclet<UserService> proclet)
    : _user_service(std::move(proclet)),
      _userid_to_followers_map(
          nu::make_dis_hash_table<int64_t, std::set<int64_t>, I64Hasher>(
              kDefaultHashTablePowerNumShards)),
      _userid_to_followees_map(
          nu::make_dis_hash_table<int64_t, std::set<int64_t>, I64Hasher>(
              kDefaultHashTablePowerNumShards)) {}

void SocialGraphService::Follow(int64_t user_id, int64_t followee_id) {
  auto add_followee_future = _userid_to_followees_map.apply_async(
      user_id,
      +[](std::pair<const int64_t, std::set<int64_t>> &p, int64_t followee_id) {
        p.second.emplace(followee_id);
      },
      followee_id);
  auto add_follower_future = _userid_to_followers_map.apply_async(
      followee_id,
      +[](std::pair<const int64_t, std::set<int64_t>> &p, int64_t user_id) {
        p.second.emplace(user_id);
      },
      user_id);
  add_followee_future.get();
  add_follower_future.get();
}

void SocialGraphService::Unfollow(int64_t user_id, int64_t followee_id) {
  auto add_followee_future = _userid_to_followees_map.apply_async(
      user_id,
      +[](std::pair<const int64_t, std::set<int64_t>> &p, int64_t followee_id) {
        p.second.erase(followee_id);
      },
      followee_id);
  auto add_follower_future = _userid_to_followers_map.apply_async(
      followee_id,
      +[](std::pair<const int64_t, std::set<int64_t>> &p, int64_t user_id) {
        p.second.erase(user_id);
      },
      user_id);
  add_followee_future.get();
  add_follower_future.get();
}

std::vector<int64_t> SocialGraphService::GetFollowers(int64_t user_id) {
  return _userid_to_followers_map.apply(
      user_id, +[](std::pair<const int64_t, std::set<int64_t>> &p) {
        auto &set = p.second;
        return std::vector<int64_t>(set.begin(), set.end());
      });
}

std::vector<int64_t> SocialGraphService::GetFollowees(int64_t user_id) {
  return _userid_to_followees_map.apply(
      user_id, +[](std::pair<const int64_t, std::set<int64_t>> &p) {
        auto &set = p.second;
        return std::vector<int64_t>(set.begin(), set.end());
      });
}

void SocialGraphService::FollowWithUsername(std::string user_name,
                                            std::string followee_name) {
  auto user_id_future = _user_service.run_async(&UserService::GetUserId,
                                                    std::move(user_name));
  auto followee_id_future = _user_service.run_async(
      &UserService::GetUserId, std::move(followee_name));
  auto user_id = user_id_future.get();
  auto followee_id = followee_id_future.get();
  if (user_id && followee_id) {
    Follow(user_id, followee_id);
  }
}

void SocialGraphService::UnfollowWithUsername(std::string user_name,
                                              std::string followee_name) {
  auto user_id_future = _user_service.run_async(&UserService::GetUserId,
                                                    std::move(user_name));
  auto followee_id_future = _user_service.run_async(
      &UserService::GetUserId, std::move(followee_name));
  auto user_id = user_id_future.get();
  auto followee_id = followee_id_future.get();
  if (user_id && followee_id) {
    Unfollow(user_id, followee_id);
  }
}

} // namespace social_network
