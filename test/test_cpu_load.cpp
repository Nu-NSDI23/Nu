#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <utility>
#include <vector>

extern "C" {
#include <net/ip.h>
}
#include <runtime.h>

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/time.hpp"

using namespace nu;

uint32_t ip = MAKE_IP_ADDR(18, 18, 1, 2);

class CPUHeavyObj {
 public:
  constexpr static uint32_t kTimeUs = 100;

  void compute() { delay_us(kTimeUs); }
};

class CPULightObj {
 public:
  constexpr static uint32_t kTimeUs = 10;

  void compute() {
    Time time;
    delay_us(kTimeUs);
  }
};

class CPUSpinObj {
 public:
  void compute() {
    while (!rt::access_once(done_)) cpu_relax();
  }

  void done() { done_ = true; }

  bool done_ = false;
};

class CPUNestedObj {
 public:
  constexpr static uint32_t kTime0Us = 40;
  constexpr static uint32_t kTime1Us = 50;
  constexpr static uint32_t kTimeUs = kTime0Us + kTime1Us;

  CPUNestedObj()
      : light_obj_(make_proclet<CPULightObj>(false, std::nullopt, ip)),
        heavy_obj_(make_proclet<CPUHeavyObj>(false, std::nullopt, ip)) {}

  void compute() {
    delay_us(kTime0Us);
    light_obj_.run(&CPULightObj::compute);
    delay_us(kTime1Us);
    heavy_obj_.run(&CPUHeavyObj::compute);
  }

  Proclet<CPULightObj> light_obj_;
  Proclet<CPUHeavyObj> heavy_obj_;
};

namespace nu {

class Test {
 public:
  void migrate() {
    rt::Preempt p;
    rt::PreemptGuard g(&p);
    get_runtime()->pressure_handler()->mock_set_pressure();
  }

  bool mostly_equal(double real, double expected) {
    return std::abs((real - expected) / real) < 0.15;
  }

  bool run() {
    bool passed = true;

    auto light_obj = make_proclet<CPULightObj>(false, std::nullopt, ip);
    auto heavy_obj = make_proclet<CPUHeavyObj>(false, std::nullopt, ip);
    auto nested_obj = make_proclet<CPUNestedObj>(false, std::nullopt, ip);
    auto spin_obj = make_proclet<CPUSpinObj>(false, std::nullopt, ip);
    auto migration_obj = make_proclet<Test>(false, std::nullopt, ip);

    auto spin_future = spin_obj.run_async(&CPUSpinObj::compute);
    for (uint32_t i = 0; i < 100000; i++) {
      auto light_future = light_obj.run_async(&CPULightObj::compute);
      auto heavy_future = heavy_obj.run_async(&CPUHeavyObj::compute);
      auto nested_future = nested_obj.run_async(&CPUNestedObj::compute);
      if (i == 50000) {
        migration_obj.run(&Test::migrate);
      }
    }

    auto light_cpu_load = light_obj.run(+[](CPULightObj &_) {
      rt::Preempt p;
      rt::PreemptGuard g(&p);
      auto *heap_header = get_runtime()->get_current_proclet_header();
      return heap_header->cpu_load.get_load();
    });

    auto heavy_cpu_load = heavy_obj.run(+[](CPUHeavyObj &_) {
      rt::Preempt p;
      rt::PreemptGuard g(&p);
      auto *heap_header = get_runtime()->get_current_proclet_header();
      return heap_header->cpu_load.get_load();
    });

    auto nested_cpu_load = nested_obj.run(+[](CPUNestedObj &_) {
      rt::Preempt p;
      rt::PreemptGuard g(&p);
      auto *heap_header = get_runtime()->get_current_proclet_header();
      return heap_header->cpu_load.get_load();
    });

    auto spin_cpu_load = spin_obj.run(+[](CPUSpinObj &_) {
      rt::Preempt p;
      rt::PreemptGuard g(&p);
      auto *heap_header = get_runtime()->get_current_proclet_header();
      CPULoad::flush_all();
      return heap_header->cpu_load.get_load();
    });
    spin_obj.run(&CPUSpinObj::done);

    passed &= mostly_equal(spin_cpu_load, 1);
    passed &= mostly_equal(heavy_cpu_load / light_cpu_load,
                           CPUHeavyObj::kTimeUs / CPULightObj::kTimeUs);
    passed &= mostly_equal(nested_cpu_load / light_cpu_load,
                           CPUNestedObj::kTimeUs / CPULightObj::kTimeUs);
    return passed;
  };
};
}  // namespace nu

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) {
    Test test;
    if (test.run()) {
      std::cout << "Passed" << std::endl;
    } else {
      std::cout << "Failed" << std::endl;
    }
  });
}
