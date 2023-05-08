#include <chrono>

#include "dataloader.hpp"

using namespace std::chrono;
using namespace imagenet;

std::string datapath = "train_t3";

void do_work() {
  auto start = high_resolution_clock::now();
  auto dataloader = DataLoader(datapath);
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(end - start);
  std::cout << "DataLoader: Image loading takes " << duration.count() << "ms"
            << std::endl;

  auto ms = dataloader.process_all();
  auto throughput = (1000 * dataloader.size()) / ms;
  // std::cout << "DataLoader: Image pre-processing takes " << ms
  //           << "ms; throughput: " << throughput << " images/s" << std::endl;
  printf("ms %lld, tput %d", (long long)ms, (int)throughput);
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
