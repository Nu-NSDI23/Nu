#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

extern "C" {
#include <net/ip.h>
#include <runtime/runtime.h>
}
#include <runtime.h>

#include "nu/dis_hash_table.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/farmhash.hpp"

using namespace nu;

constexpr size_t kNumPairs = 100000;
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

bool run_test() {
  std::unordered_map<std::string, std::string> std_map;
  auto hash_table = make_dis_hash_table<std::string, std::string>(5);
  for (uint32_t i = 0; i < kNumPairs; i++) {
    std::string k = random_str(kKeyLen);
    std::string v = random_str(kValLen);
    std_map[k] = v;
    hash_table.put(k, v);
  }

  auto hash_table2 = hash_table;

  for (auto &[k, v] : std_map) {
    auto optional = hash_table2.get(k);
    if (!optional || v != *optional) {
      return false;
    }
  }

  auto proclet = make_proclet<ErasedType>();
  if (!proclet.run(
          +[](ErasedType &,
              std::unordered_map<std::string, std::string> std_map,
              DistributedHashTable<std::string, std::string> hash_table) {
            for (auto &[k, v] : std_map) {
              auto optional = hash_table.get(k);
              if (!optional || v != *optional) {
                return false;
              }
            }
            return true;
          },
          std_map, hash_table2)) {
    return false;
  }

  auto hash_table_3 = std::move(hash_table2);
  for (auto &[k, v] : std_map) {
    auto optional = hash_table_3.get(k);
    if (!optional || v != *optional) {
      return false;
    }
  }

  std::set<std::pair<std::string, std::string>> std_set;
  for (auto &[k, v] : std_map) {
    std_set.emplace(k, v);
  }
  auto all_pairs = hash_table.get_all_pairs();
  std::set<std::pair<std::string, std::string>> our_set(all_pairs.begin(),
                                                        all_pairs.end());
  if (std_set != our_set) {
    return false;
  }

  our_set.clear();
  our_set = hash_table.associative_reduce(
      /* clear = */ false, /* init_val = */ our_set,
      /* reduce_fn = */
      +[](std::set<std::pair<std::string, std::string>> &set,
          std::pair<const K, V> &pair) {
        set.emplace(pair.first, pair.second);
      },
      /* merge_fn = */
      +[](std::set<std::pair<std::string, std::string>> &set,
          std::set<std::pair<std::string, std::string>> &pairs) {
        set.insert(pairs.begin(), pairs.end());
      });
  if (std_set != our_set) {
    return false;
  }

  for (auto &[k, _] : std_map) {
    if (!hash_table_3.remove(k)) {
      return false;
    }
  }
  return true;
}

void do_work() {
  if (run_test()) {
    std::cout << "Passed" << std::endl;
  } else {
    std::cout << "Failed" << std::endl;
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
