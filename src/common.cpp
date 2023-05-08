extern "C" {
#include <base/assert.h>
#include <base/stddef.h>
#include <net/ip.h>
}

#include "nu/commons.hpp"

namespace nu {

uint32_t str_to_ip(std::string ip_str) {
  auto pos0 = ip_str.find('.');
  BUG_ON(pos0 == std::string::npos);
  auto pos1 = ip_str.find('.', pos0 + 1);
  BUG_ON(pos1 == std::string::npos);
  auto pos2 = ip_str.find('.', pos1 + 1);
  BUG_ON(pos2 == std::string::npos);
  auto addr0 = stoi(ip_str.substr(0, pos0));
  auto addr1 = stoi(ip_str.substr(pos0 + 1, pos1 - pos0));
  auto addr2 = stoi(ip_str.substr(pos1 + 1, pos2 - pos1));
  auto addr3 = stoi(ip_str.substr(pos2 + 1));
  return MAKE_IP_ADDR(addr0, addr1, addr2, addr3);
}

}  // namespace nu
