#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <tuple>
#include <vector>

extern "C" {
#include <net/ip.h>
}
#include <runtime.h>

#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

using namespace nu;

class Adder {
 public:
  std::vector<int> add(const std::vector<int> vec_a,
                       const std::vector<int> vec_b) {
    std::vector<int> vec_c;
    for (size_t i = 0; i < vec_a.size(); i++) {
      vec_c.push_back(vec_a[i] + vec_b[i]);
    }
    return vec_c;
  }
};

class VecStore {
 public:
  VecStore(const std::vector<int> &a, const std::vector<int> &b)
      : a_(a), b_(b) {
    adder = make_proclet<Adder>();
  }
  std::vector<int> get_vec_a() { return a_; }
  std::vector<int> get_vec_b() { return b_; }

  std::vector<int> add_vec() { 
    return adder.run(
        +[](Adder &adder, std::vector<int> a_, std::vector<int> b_) {
          return adder.add(a_, b_);
        },
        a_, b_
    ); 
  }

  std::vector<int> add_vec_nonclosure() {
    return adder.run(&Adder::add, a_, b_);
  }

 private:
  std::vector<int> a_;
  std::vector<int> b_;
  Proclet<Adder> adder;
};

void do_work() {
  bool passed = true;

  std::vector<int> a{1, 2, 3, 4};
  std::vector<int> b{5, 6, 7, 8};

  auto rem_vec = make_proclet<VecStore>(std::forward_as_tuple(a, b));
  auto c = rem_vec.run(&VecStore::add_vec);

  for (size_t i = 0; i < a.size(); i++) {
    if (c[i] != a[i] + b[i]) {
      passed = false;
      break;
    }
  }

  auto c_nonclosure = rem_vec.run(&VecStore::add_vec_nonclosure);
  for (size_t i = 0; i < a.size(); i++) {
    if (c_nonclosure[i] != a[i] + b[i]) {
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
