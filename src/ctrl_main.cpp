#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <utility>

extern "C" {
#include <runtime/net.h>
}
#include <runtime.h>

#include "nu/command_line.hpp"
#include "nu/commons.hpp"
#include "nu/ctrl_server.hpp"
#include "nu/runtime.hpp"

constexpr auto kDefaultNumGuaranteedCores = 2;
constexpr auto kDefaultNumSpinningCores = 2;
constexpr auto kIP = "18.18.1.1";

namespace nu {

int ctrl_main(int argc, char **argv) {
  nu::CaladanOptionsDesc desc(kDefaultNumGuaranteedCores,
                              kDefaultNumSpinningCores, kIP);
  desc.parse(argc, argv);

  auto conf_path = desc.conf_path;
  if (conf_path.empty()) {
    conf_path = ".conf_" + std::to_string(getpid());
    write_options_to_file(conf_path, desc);
  }

  auto ret = rt::RuntimeInit(conf_path, [&] {
    if (conf_path.starts_with(".conf_")) {
      BUG_ON(remove(conf_path.c_str()));
    }
    new (get_runtime())
        Runtime(get_cfg_ip(), nu::Runtime::Mode::kController, 0, false);
    std::unreachable();
  });

  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}

}  // namespace nu

int main(int argc, char **argv) { return nu::ctrl_main(argc, argv); }
