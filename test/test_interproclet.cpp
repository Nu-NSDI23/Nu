#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

extern "C" {
#include <net/ip.h>
}
#include <runtime.h>

#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

using namespace nu;

class VecStore {
 public:
  VecStore(const std::vector<int> &a, const std::vector<int> &b)
      : a_(a), b_(b) {}
  std::vector<int> get_vec_a() { return a_; }
  std::vector<int> get_vec_b() { return b_; }

 private:
  std::vector<int> a_;
  std::vector<int> b_;
};

class Adder {
 public:
  std::vector<int> add(const std::vector<int> &vec_a,
                       const std::vector<int> &vec_b) {
    std::vector<int> vec_c;
    for (size_t i = 0; i < vec_a.size(); i++) {
      vec_c.push_back(vec_a[i] + vec_b[i]);
    }
    return vec_c;
  }
};

void do_work() {
  bool passed = true;

  std::vector<int> a{1, 2, 3, 4};
  std::vector<int> b{5, 6, 7, 8};

  auto rem_vec = make_proclet<VecStore>(std::forward_as_tuple(a, b));
  auto rem_adder = make_proclet<Adder>();
  auto c = rem_adder.run(
      +[](Adder &adder, Proclet<VecStore> rem_vec) {
        auto vec_a = rem_vec.run(&VecStore::get_vec_a);
        auto vec_b = rem_vec.run(&VecStore::get_vec_b);
        return adder.add(vec_a, vec_b);
      },
      rem_vec);

  for (size_t i = 0; i < a.size(); i++) {
    if (c[i] != a[i] + b[i]) {
      passed = false;
      break;
    }
  }

  if (passed) {
    std::cout << "Passed" << std::endl;
  } else {
    std::cout << "Failed" << std::endl;
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
