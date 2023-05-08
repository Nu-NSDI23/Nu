#pragma once

extern "C" {
#include <runtime/net.h>
}

namespace std {

template <>
struct hash<netaddr> {
  std::size_t operator()(const netaddr &k) const {
    return (static_cast<std::size_t>(k.ip) << 16) | k.port;
  }
};
}  // namespace std

inline bool operator==(netaddr x, netaddr y) {
  return x.ip == y.ip && x.port == y.port;
}

inline bool operator<(netaddr x, netaddr y) {
  return std::hash<netaddr>{}(x) < std::hash<netaddr>{}(y);
}
