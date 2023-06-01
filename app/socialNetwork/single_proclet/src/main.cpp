#include <iostream>
#include <memory>
#include <nu/proclet.hpp>
#include <nu/runtime.hpp>

#include "ThriftBackEndServer.hpp"

constexpr uint32_t kNumEntryObjs = 1;

using namespace social_network;

class ServiceEntry {
public:
  ServiceEntry(States states) {
    json config_json;

    BUG_ON(LoadConfigFile("config/service-config.json", &config_json) != 0);

    auto port = config_json["back-end-service"]["port"];
    std::cout << "port = " << port << std::endl;
    std::shared_ptr<TServerSocket> server_socket =
        std::make_shared<TServerSocket>("0.0.0.0", port);

    states.secret = config_json["secret"];
    auto back_end_handler =
        std::make_shared<ThriftBackEndServer>(std::move(states));

    TThreadedServer server(
        std::make_shared<BackEndServiceProcessor>(std::move(back_end_handler)),
        server_socket, std::make_shared<TFramedTransportFactory>(),
        std::make_shared<TBinaryProtocolFactory>());
    std::cout << "Starting the ThriftBackEndServer..." << std::endl;
    server.serve();
  }
};

void DoWork() {
  States states;

  std::vector<nu::Future<nu::Proclet<ServiceEntry>>> thrift_futures;
  for (uint32_t i = 0; i < kNumEntryObjs; i++) {
    thrift_futures.emplace_back(nu::make_proclet_async<ServiceEntry>(
        std::forward_as_tuple(states), true));
  }
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { DoWork(); });
}
