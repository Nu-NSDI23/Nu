#include <iostream>
#include <memory>
#include <nu/proclet.hpp>
#include <nu/runtime.hpp>

#include "BackEndService.hpp"
#include "client.hpp"
#include "initializer.hpp"

constexpr uint32_t kNumClients = 1;
constexpr uint32_t kClientIPs[] = {MAKE_IP_ADDR(18, 18, 1, 100)};

using namespace social_network;

void DoWork() {
  States states;
  auto backend =
      nu::make_proclet<BackEndService>(std::forward_as_tuple(states));
  {
    auto initializer =
        nu::make_proclet<Initializer>(std::forward_as_tuple(backend));
    initializer.run(&Initializer::init);
  }
  auto client = nu::make_proclet<Client>(std::forward_as_tuple(backend), true,
                                         std::nullopt, kClientIPs[0]);
  client.run(&Client::bench);
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { DoWork(); });
}
