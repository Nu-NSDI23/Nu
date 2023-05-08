#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

#include "nu/runtime.hpp"
#include "nu/utils/farmhash.hpp"
#include "nu/utils/sync_hash_map.hpp"

using namespace nu;

constexpr size_t NBuckets = 262144;
constexpr double kLoadFactor = 0.25;
constexpr size_t kNumPairs = 262144 * kLoadFactor;
constexpr uint32_t kKeyLen = 20;
constexpr uint32_t kValLen = 2;
constexpr auto kFarmHashStrtoU64 = [](const std::string &str) {
  return util::Hash64(str.c_str(), str.size());
};

using K = std::string;
using V = std::string;

std::random_device rd;
std::mt19937 mt(rd());
std::uniform_int_distribution<int> dist('A', 'z');

std::string random_str(uint32_t len) {
  std::string str = "";
  for (uint32_t i = 0; i < len; i++) {
    str += dist(mt);
  }
  return str;
}

void do_work() {
  auto map_ptr = std::make_unique<
      SyncHashMap<NBuckets, K, V, decltype(kFarmHashStrtoU64)>>();
  std::unordered_map<std::string, std::string> std_map;

  std::cout << "Running " << __FILE__ "..." << std::endl;
  bool passed = true;

  for (uint32_t i = 0; i < kNumPairs; i++) {
    std::string k = random_str(kKeyLen);
    std::string v = random_str(kValLen);
    std_map[k] = v;
    map_ptr->put(k, v);
  }

  for (auto &[k, v] : std_map) {
    auto optional = map_ptr->get(k);
    if (!optional || v != *optional) {
      passed = false;
      goto done;
    }
  }

  for (auto &[k, _] : std_map) {
    if (!map_ptr->remove(k)) {
      passed = false;
      goto done;
    }
  }

done:
  if (passed) {
    std::cout << "Passed" << std::endl;
  } else {
    std::cout << "Failed" << std::endl;
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
