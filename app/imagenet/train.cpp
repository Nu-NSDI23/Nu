// pretend to be a super cool ML training script
#include "dataloader.hpp"

std::string datapath = "train_t3";
size_t batch_size = 32;
size_t train_size = 1024;

void step(Image data) {
  // out = net.forward(data)
  // loss = criterion(out, labels)
  // loss.backward()
  // optimizer.step()
  static i = 0;
  std::cout << "image" << i << std::endl;
  i++;
}

void train() {
  auto dataloader = DataLoader(datapath, batch_size);

  for (int batch = 0; batch < train_size / batch_size; batch++) {
    auto data = dataloader.next();
    step(data);
  }
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { train(); });
}