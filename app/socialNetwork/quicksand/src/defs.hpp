#pragma once

#include <ext/pb_ds/assoc_container.hpp>

#define HOSTNAME "http://short-url/"
// Custom Epoch (January 1, 2018 Midnight GMT = 2018-01-01T00:00:00Z)
#define CUSTOM_EPOCH 1514764800000

namespace social_network {

struct Url {
  std::string shortened_url;
  std::string expanded_url;

  template <class Archive>
  void serialize(Archive &ar) {
    ar(shortened_url, expanded_url);
  }
};

struct Creator {
  int64_t user_id;
  std::string username;

  template <class Archive>
  void serialize(Archive &ar) {
    ar(user_id, username);
  }
};

struct UserMention {
  int64_t user_id;
  std::string username;

  template <class Archive>
  void serialize(Archive &ar) {
    ar(user_id, username);
  }
};

struct Media {
  int64_t media_id;
  std::string media_type;

  template <class Archive>
  void serialize(Archive &ar) {
    ar(media_id, media_type);
  }
};

struct TextServiceReturn {
  std::string text;
  std::vector<UserMention> user_mentions;
  std::vector<Url> urls;

  template <class Archive>
  void serialize(Archive &ar) {
    ar(text, user_mentions, urls);
  }
};

struct PostType {
  enum type {
    POST = 0,
    REPOST = 1,
    REPLY = 2,
    DM = 3
  };
};

struct Post {
  int64_t post_id;
  Creator creator;
  int64_t req_id;
  std::string text;
  std::vector<UserMention> user_mentions;
  std::vector<Media> media;
  std::vector<Url> urls;
  int64_t timestamp;
  PostType::type post_type;

  template <class Archive>
  void serialize(Archive &ar) {
    ar(post_id, creator, req_id, text, user_mentions, media, urls, timestamp,
       post_type);
  }
};

using Timeline =
    __gnu_pbds::tree<std::pair<int64_t, int64_t>, __gnu_pbds::null_type,
                     std::greater<std::pair<int64_t, int64_t>>,
                     __gnu_pbds::rb_tree_tag,
                     __gnu_pbds::tree_order_statistics_node_update>;

struct UserProfile {
  int64_t user_id;
  std::string first_name;
  std::string last_name;
  std::string salt;
  std::string password_hashed;

  template <class Archive> void serialize(Archive &ar) {
    ar(user_id, first_name, last_name, salt, password_hashed);
  }
};

enum LoginErrorCode { OK, NOT_REGISTERED, WRONG_PASSWORD };

} // namespace social_network
