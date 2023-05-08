#include <atomic>
#include <iostream>

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/proclet_server.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/thread.hpp"
#include "nu/utils/time.hpp"

constexpr uint32_t kMagic = 0x12345678;
constexpr uint32_t ip = MAKE_IP_ADDR(18, 18, 1, 2);

namespace nu {

class CalleeObj {
 public:
  uint32_t foo() {
    Time::delay_us(1000 * 1000);
    return kMagic;
  }
};

class CallerObj {
 public:
  CallerObj() {}

  uint32_t foo(Proclet<CalleeObj> callee_obj) {
    return callee_obj.run(&CalleeObj::foo);
  }
};

class Test {
 public:
  bool run_callee_migrated_test() {
    auto caller_obj = make_proclet<CallerObj>(true, std::nullopt, ip);
    auto callee_obj = make_proclet<CalleeObj>(false, std::nullopt, ip);
    auto future = caller_obj.run_async(&CallerObj::foo, callee_obj);
    delay_us(500 * 1000);
    callee_obj.run(+[](CalleeObj &_) { Test::migrate(); });
    return future.get() == kMagic;
  }

  bool run_caller_migrated_test() {
    auto caller_obj = make_proclet<CallerObj>(false, std::nullopt, ip);
    auto callee_obj = make_proclet<CalleeObj>(true, std::nullopt, ip);
    auto future = caller_obj.run_async(&CallerObj::foo, std::move(callee_obj));
    delay_us(500 * 1000);
    caller_obj.run(+[](CallerObj &_) { Test::migrate(); });
    return future.get() == kMagic;
  }

  bool run_both_migrated_test() {
    auto caller_obj = make_proclet<CallerObj>(false, std::nullopt, ip);
    auto callee_obj = make_proclet<CalleeObj>(false, std::nullopt, ip);
    auto future = caller_obj.run_async(&CallerObj::foo, callee_obj);
    delay_us(500 * 1000);
    caller_obj.run(+[](CallerObj &_) { Test::migrate(); });
    callee_obj.run(+[](CalleeObj &_) { Test::migrate(); });
    return future.get() == kMagic;
  }

  bool run_all_tests() {
    return run_callee_migrated_test() && run_caller_migrated_test() &&
           run_both_migrated_test();
  }

  static void migrate() {
    rt::Preempt p;
    rt::PreemptGuard g(&p);
    get_runtime()->pressure_handler()->mock_set_pressure();
  }
};

}  // namespace nu

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) {
    nu::Test test;
    if (test.run_all_tests()) {
      std::cout << "Passed" << std::endl;
    } else {
      std::cout << "Failed" << std::endl;
    }
  });
}
