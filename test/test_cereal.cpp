#include <iostream>
#include <sstream>

#include "nu/runtime.hpp"

using namespace nu;

struct Serializable {
 public:
  bool flag = false;

  template <class Archive>
  void serialize(Archive &archive) {
    flag = true;
  }
};

struct SaveLoadable {
 public:
  bool flag0 = false;
  bool flag1 = false;
  bool flag2 = false;

  template <class Archive>
  void save(Archive &archive) const {
    const_cast<SaveLoadable *>(this)->flag0 = true;
  }

  template <class Archive>
  void save_move(Archive &archive) {
    flag1 = true;
  }
};

bool run() {
  std::stringstream ss;
  cereal::BinaryOutputArchive oa(ss);

  Serializable ser;
  oa << ser;
  if (!ser.flag) {
    return false;
  }

  SaveLoadable sl_0;
  oa << sl_0;
  if (!sl_0.flag0 || sl_0.flag1) {
    return false;
  }

  SaveLoadable sl_1;
  oa << std::move(sl_1);
  if (sl_1.flag0 || !sl_1.flag1) {
    return false;
  }

  return true;
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) {
    if (run()) {
      std::cout << "Passed" << std::endl;
    } else {
      std::cout << "Failed" << std::endl;
    }
  });
}
