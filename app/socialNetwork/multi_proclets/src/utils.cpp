#include <sstream>

#include "utils.h"

namespace social_network {

int load_config_file(const std::string &file_name, json *config_json) {
  std::ifstream json_file;
  json_file.open(file_name);
  if (json_file.is_open()) {
    json_file >> *config_json;
    json_file.close();
    return 0;
  } else {
    std::cerr << "Cannot open service-config.json" << std::endl;
    return -1;
  }
}

/*
 * The following code which obtaines machine ID from machine's MAC address was
 * inspired from https://stackoverflow.com/a/16859693.
 *
 * MAC address is obtained from /sys/class/net/<netif>/address
 */
u_int16_t HashMacAddressPid(const std::string &mac) {
  u_int16_t hash = 0;
  std::string mac_pid = mac + std::to_string(getpid());
  for (unsigned int i = 0; i < mac_pid.size(); i++) {
    hash += (mac[i] << ((i & 1) * 8));
  }
  return hash;
}

std::string GetMachineId(std::string &netif) {
  std::string mac_hash;

  std::string mac_addr_filename = "/sys/class/net/" + netif + "/address";
  std::ifstream mac_addr_file;
  mac_addr_file.open(mac_addr_filename);
  if (!mac_addr_file) {
    std::cerr << "Cannot read MAC address from net interface " << netif
              << std::endl;
    return "";
  }
  std::string mac;
  mac_addr_file >> mac;
  if (mac == "") {
    std::cerr << "Cannot read MAC address from net interface " << netif
              << std::endl;
    return "";
  }
  mac_addr_file.close();

  std::cerr << "MAC address = " << mac << std::endl;

  std::stringstream stream;
  stream << std::hex << HashMacAddressPid(mac);
  mac_hash = std::move(stream.str());

  if (mac_hash.size() > 3) {
    mac_hash.erase(0, mac_hash.size() - 3);
  } else if (mac_hash.size() < 3) {
    mac_hash = std::string(3 - mac_hash.size(), '0') + mac_hash;
  }
  return mac_hash;
}

} // namespace social_network
