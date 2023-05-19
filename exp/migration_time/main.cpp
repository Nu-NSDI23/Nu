#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

extern "C" {
#include <base/time.h>
#include <net/ip.h>
#include <runtime/runtime.h>
}
#include <runtime.h>

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

constexpr uint32_t kObjSize = 16777216;
constexpr uint32_t kNumObjs = 1024;

class Obj {
public:
  uint32_t get_ip() { return get_cfg_ip(); }
private:
  uint8_t bytes[kObjSize];
};

class Migrator {
 public:
  void migrate() { nu::get_runtime()->pressure_handler()->mock_set_pressure(); }
};

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) {
    auto l_ip = MAKE_IP_ADDR(18, 18, 1, 2);
    auto r_ip = MAKE_IP_ADDR(18, 18, 1, 3);
    std::vector<nu::Proclet<Obj>> objs;
    for (uint32_t i = 0; i < kNumObjs; i++) {
      objs.emplace_back(nu::make_proclet<Obj>(false, std::nullopt, l_ip));
    }
    auto migrator = nu::make_proclet<Migrator>(true, std::nullopt, l_ip);
    migrator.run(&Migrator::migrate);

  retry:
    for (auto &obj : objs) {
      if (obj.run(&Obj::get_ip) != r_ip) {
        std::cout << obj.run(&Obj::get_ip) << " " << r_ip << std::endl;
        timer_sleep(1000 * 1000);
        goto retry;
      }
    }
  });
}
