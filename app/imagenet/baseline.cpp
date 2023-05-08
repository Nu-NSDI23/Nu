#include <thread.h>
#include <chrono>
#include <nu/runtime.hpp>

#include "dataloader.hpp"

using namespace std::chrono;
using namespace imagenet;

std::string datapath = "train_t3";
constexpr auto kNumThreads = 1000;

void do_work() {
  auto start = high_resolution_clock::now();
  auto dataloader = BaselineDataLoader(datapath, kNumThreads);
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(end - start);
  std::cout << "BaselineDataLoader: Image loading takes " << duration.count()
            << "ms" << std::endl;

  start = high_resolution_clock::now();
  dataloader.process_all();
  end = high_resolution_clock::now();
  duration = duration_cast<milliseconds>(end - start);
  std::cout << "BaselineDataLoader: Image pre-processing takes "
            << duration.count() << "ms" << std::endl;
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
