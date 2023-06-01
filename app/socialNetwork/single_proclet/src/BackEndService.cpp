#include <PicoSHA2/picosha2.h>
#include <iostream>

#include "BackEndService.hpp"

namespace social_network {

BackEndService::BackEndService(States states) : states_(std::move(states)) {}

void BackEndService::ComposePost(const std::string &username, int64_t user_id,
                                 const std::string &text,
                                 const std::vector<int64_t> &media_ids,
                                 const std::vector<std::string> &media_types,
                                 const PostType::type post_type) {
  auto text_service_return_future =
      nu::async([&] { return ComposeText(text); });

  auto timestamp = rdtsc();
  auto unique_id = GenUniqueId();

  auto write_user_timeline_future = nu::async(
      [&] { return WriteUserTimeline(unique_id, user_id, timestamp); });

  std::vector<Media> medias;
  BUG_ON(media_types.size() != media_ids.size());
  for (int i = 0; i < media_ids.size(); ++i) {
    Media media;
    media.media_id = media_ids[i];
    media.media_type = media_types[i];
    medias.emplace_back(media);
  }

  Post post;
  post.timestamp = timestamp;
  post.post_id = unique_id;
  post.media = std::move(medias);
  post.creator.username = username;
  post.creator.user_id = user_id;
  post.post_type = post_type;

  auto &text_service_return = text_service_return_future.get();
  std::vector<int64_t> user_mention_ids;
  for (auto &item : text_service_return.user_mentions) {
    user_mention_ids.emplace_back(item.user_id);
  }
  auto write_home_timeline_future = nu::async([&] {
    return WriteHomeTimeline(unique_id, user_id, timestamp, user_mention_ids);
  });

  post.text = std::move(text_service_return.text);
  post.urls = std::move(text_service_return.urls);
  post.user_mentions = std::move(text_service_return.user_mentions);

  auto post_future =
      states_.postid_to_post_map.put_async(post.post_id, std::move(post));

  write_user_timeline_future.get();
  write_home_timeline_future.get();
  post_future.get();
}

TextServiceReturn BackEndService::ComposeText(const std::string &text) {
  auto target_urls_future =
      nu::async([&] { return ComposeUrls(MatchUrls(text)); });
  auto user_mentions_future =
      nu::async([&] { return ComposeUserMentions(MatchMentions(text)); });
  auto &target_urls = target_urls_future.get();
  auto updated_text = ShortenUrlInText(text, target_urls);
  TextServiceReturn text_service_return;
  text_service_return.user_mentions = std::move(user_mentions_future.get());
  text_service_return.text = std::move(updated_text);
  text_service_return.urls = std::move(target_urls);
  return text_service_return;
}

std::vector<UserMention>
BackEndService::ComposeUserMentions(const std::vector<std::string> &usernames) {
  std::vector<nu::Future<std::optional<UserProfile>>>
      user_profile_optional_futures;
  for (auto &username : usernames) {
    user_profile_optional_futures.emplace_back(
        states_.username_to_userprofile_map.get_async(username));
  }

  std::vector<UserMention> user_mentions;
  for (size_t i = 0; i < user_profile_optional_futures.size(); i++) {
    auto &user_profile_optional = user_profile_optional_futures[i].get();
    BUG_ON(!user_profile_optional);
    auto &user_profile = *user_profile_optional;
    user_mentions.emplace_back();
    auto &user_mention = user_mentions.back();
    user_mention.username = std::move(usernames[i]);
    user_mention.user_id = user_profile.user_id;
  }

  return user_mentions;
}

std::vector<Url>
BackEndService::ComposeUrls(const std::vector<std::string> &urls) {
  std::vector<nu::Future<Url>> target_url_futures;

  for (ssize_t i = 0; i < urls.size(); i++) {
    target_url_futures.emplace_back(nu::async([&, i] {
      Url target_url;
      target_url.expanded_url = urls[i];
      target_url.shortened_url = HOSTNAME + random_string_generator_.Gen(10);
      states_.short_to_extended_map.put(target_url.shortened_url,
                                        target_url.expanded_url);
      return target_url;
    }));
  }

  std::vector<Url> target_urls;
  for (auto &target_url_future : target_url_futures) {
    target_urls.emplace_back(std::move(target_url_future.get()));
  }
  return target_urls;
}

void BackEndService::WriteUserTimeline(int64_t post_id, int64_t user_id,
                                       int64_t timestamp) {
  states_.userid_to_usertimeline_map.apply(
      user_id,
      +[](std::pair<const int64_t, Timeline> &p, int64_t timestamp,
          int64_t post_id) {
        (p.second).insert(std::make_pair(timestamp, post_id));
      },
      timestamp, post_id);
}

std::vector<Post> BackEndService::ReadUserTimeline(int64_t user_id, int start,
                                                   int stop) {
  if (stop <= start || start < 0) {
    return std::vector<Post>();
  }

  auto post_ids = states_.userid_to_usertimeline_map.apply(
      user_id,
      +[](std::pair<const int64_t, Timeline> &p, int start, int stop) {
        auto start_iter = p.second.find_by_order(start);
        auto stop_iter = p.second.find_by_order(stop);
        std::vector<int64_t> post_ids;
        for (auto iter = start_iter; iter != stop_iter; iter++) {
          post_ids.push_back(iter->second);
        }
        return post_ids;
      },
      start, stop);
  return ReadPosts(post_ids);
}

void BackEndService::RemovePosts(int64_t user_id, int start, int stop) {
  auto posts_future =
      nu::async([&]() { return ReadUserTimeline(user_id, start, stop); });
  auto followers_future = nu::async([&]() { return GetFollowers(user_id); });
  auto posts = std::move(posts_future.get());
  auto followers = std::move(followers_future.get());

  std::vector<nu::Future<bool>> remove_post_futures;
  std::vector<nu::Future<void>> remove_from_timeline_futures;
  std::vector<nu::Future<bool>> remove_short_url_futures;

  for (auto post : posts) {
    remove_post_futures.emplace_back(
        states_.postid_to_post_map.remove_async(post.post_id));

    auto remove_from_timeline_fn =
        [&remove_from_timeline_futures](
            nu::DistributedHashTable<int64_t, Timeline, I64Hasher>
                &timeline_map,
            int64_t user_id, Post &post) {
          remove_from_timeline_futures.emplace_back(timeline_map.apply_async(
              user_id,
              +[](std::pair<const int64_t, Timeline> &p, int64_t timestamp,
                  int64_t post_id) {
                (p.second).erase(std::make_pair(timestamp, post_id));
              },
              post.timestamp, post.post_id));
        };
    remove_from_timeline_fn(states_.userid_to_usertimeline_map, user_id, post);
    for (auto mention : post.user_mentions) {
      remove_from_timeline_fn(states_.userid_to_hometimeline_map,
                              mention.user_id, post);
    }
    for (auto user_id : followers) {
      remove_from_timeline_fn(states_.userid_to_hometimeline_map, user_id,
                              post);
    }

    for (auto &url : post.urls) {
      remove_short_url_futures.emplace_back(
          states_.short_to_extended_map.remove_async(url.shortened_url));
    }
  }

  for (auto &future : remove_post_futures) {
    future.get();
  }
  for (auto &future : remove_from_timeline_futures) {
    future.get();
  }
  for (auto &future : remove_short_url_futures) {
    future.get();
  }
}

void BackEndService::WriteHomeTimeline(
    int64_t post_id, int64_t user_id, int64_t timestamp,
    const std::vector<int64_t> &user_mentions_id) {
  auto follower_ids_future = nu::async([&] { return GetFollowers(user_id); });

  std::vector<nu::Future<void>> futures;

  auto future_constructor = [&](int64_t id) {
    return states_.userid_to_hometimeline_map.apply_async(
        id,
        +[](std::pair<const int64_t, Timeline> &p, int64_t timestamp,
            int64_t post_id) {
          (p.second).insert(std::make_pair(timestamp, post_id));
        },
        timestamp, post_id);
  };

  for (auto id : user_mentions_id) {
    futures.emplace_back(future_constructor(id));
  }

  auto follower_ids = follower_ids_future.get();
  for (auto id : follower_ids) {
    futures.emplace_back(future_constructor(id));
  }

  for (auto &future : futures) {
    future.get();
  }
}

std::vector<Post> BackEndService::ReadHomeTimeline(int64_t user_id, int start,
                                                   int stop) {
  if (stop <= start || start < 0) {
    return std::vector<Post>();
  }

  auto post_ids = states_.userid_to_hometimeline_map.apply(
      user_id,
      +[](std::pair<const int64_t, Timeline> &p, int start, int stop) {
        auto start_iter = p.second.find_by_order(start);
        auto stop_iter = p.second.find_by_order(stop);
        std::vector<int64_t> post_ids;
        for (auto iter = start_iter; iter != stop_iter; iter++) {
          post_ids.push_back(iter->second);
        }
        return post_ids;
      },
      start, stop);
  return ReadPosts(post_ids);
}

std::vector<Post>
BackEndService::ReadPosts(const std::vector<int64_t> &post_ids) {
  std::vector<nu::Future<std::optional<Post>>> post_futures;
  for (auto post_id : post_ids) {
    post_futures.emplace_back(states_.postid_to_post_map.get_async(post_id));
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

void BackEndService::Follow(int64_t user_id, int64_t followee_id) {
  auto add_followee_future = states_.userid_to_followees_map.apply_async(
      user_id,
      +[](std::pair<const int64_t, std::set<int64_t>> &p, int64_t followee_id) {
        p.second.emplace(followee_id);
      },
      followee_id);
  auto add_follower_future = states_.userid_to_followers_map.apply_async(
      followee_id,
      +[](std::pair<const int64_t, std::set<int64_t>> &p, int64_t user_id) {
        p.second.emplace(user_id);
      },
      user_id);
  add_followee_future.get();
  add_follower_future.get();
}

void BackEndService::Unfollow(int64_t user_id, int64_t followee_id) {
  auto add_followee_future = states_.userid_to_followees_map.apply_async(
      user_id,
      +[](std::pair<const int64_t, std::set<int64_t>> &p, int64_t followee_id) {
        p.second.erase(followee_id);
      },
      followee_id);
  auto add_follower_future = states_.userid_to_followers_map.apply_async(
      followee_id,
      +[](std::pair<const int64_t, std::set<int64_t>> &p, int64_t user_id) {
        p.second.erase(user_id);
      },
      user_id);
  add_followee_future.get();
  add_follower_future.get();
}

std::vector<int64_t> BackEndService::GetFollowers(int64_t user_id) {
  return states_.userid_to_followers_map.apply(
      user_id, +[](std::pair<const int64_t, std::set<int64_t>> &p) {
        auto &set = p.second;
        return std::vector<int64_t>(set.begin(), set.end());
      });
}

std::vector<int64_t> BackEndService::GetFollowees(int64_t user_id) {
  return states_.userid_to_followees_map.apply(
      user_id, +[](std::pair<const int64_t, std::set<int64_t>> &p) {
        auto &set = p.second;
        return std::vector<int64_t>(set.begin(), set.end());
      });
}

void BackEndService::FollowWithUsername(const std::string &user_name,
                                        const std::string &followee_name) {
  auto user_id_future = GetUserId(user_name);
  auto followee_id_future = GetUserId(followee_name);
  auto user_id = user_id_future.get();
  auto followee_id = followee_id_future.get();
  if (user_id && followee_id) {
    Follow(user_id, followee_id);
  }
}

void BackEndService::UnfollowWithUsername(const std::string &user_name,
                                          const std::string &followee_name) {
  auto user_id_future = GetUserId(user_name);
  auto followee_id_future = GetUserId(followee_name);
  auto user_id = user_id_future.get();
  auto followee_id = followee_id_future.get();
  if (user_id && followee_id) {
    Unfollow(user_id, followee_id);
  }
}

void BackEndService::RegisterUserWithId(const std::string &first_name,
                                        const std::string &last_name,
                                        const std::string &username,
                                        const std::string &password,
                                        int64_t user_id) {
  UserProfile user_profile;
  user_profile.first_name = first_name;
  user_profile.last_name = last_name;
  user_profile.user_id = user_id;
  user_profile.salt = random_string_generator_.Gen(32);
  user_profile.password_hashed =
      picosha2::hash256_hex_string(password + user_profile.salt);
  states_.username_to_userprofile_map.put(username, user_profile);
}

void BackEndService::RegisterUser(const std::string &first_name,
                                  const std::string &last_name,
                                  const std::string &username,
                                  const std::string &password) {
  auto user_id = (GenUniqueId() << 16);
  RegisterUserWithId(first_name, last_name, username, password, user_id);
}

std::variant<LoginErrorCode, std::string>
BackEndService::Login(const std::string &username,
                      const std::string &password) {
  std::string signature;
  auto user_profile_optional = states_.username_to_userprofile_map.get(username);
  if (!user_profile_optional) {
    return NOT_REGISTERED;
  }
  if (VerifyLogin(signature, *user_profile_optional, username, password,
                  states_.secret)) {
    return signature;
  } else {
    return WRONG_PASSWORD;
  }
}

nu::Future<int64_t> BackEndService::GetUserId(const std::string &username) {
  return nu::async([&] {
    auto user_id_optional = states_.username_to_userprofile_map.get(username);
    BUG_ON(!user_id_optional);
    return user_id_optional->user_id;
  });
}

void BackEndService::UploadMedia(const std::string &filename,
                                 const std::string &data) {
  states_.filename_to_data_map.put(filename, data);
}

std::string BackEndService::GetMedia(const std::string &filename) {
  auto optional = states_.filename_to_data_map.get(filename);
  return optional.value_or("");
}

} // namespace social_network
