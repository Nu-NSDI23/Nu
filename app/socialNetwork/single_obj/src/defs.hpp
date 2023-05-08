#pragma once

#include <ext/pb_ds/assoc_container.hpp>

#define HOSTNAME "http://short-url/"
// Custom Epoch (January 1, 2018 Midnight GMT = 2018-01-01T00:00:00Z)
#define CUSTOM_EPOCH 1514764800000

namespace social_network {

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
