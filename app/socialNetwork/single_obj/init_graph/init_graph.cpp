#include <fstream>
#include <limits>
#include <memory>
#include <nu/commons.hpp>
#include <nu/utils/future.hpp>
#include <random>
#include <runtime.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include "../gen-cpp/BackEndService.h"
#include "../gen-cpp/social_network_types.h"

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

const std::string kFilePath =
    "datasets/social-graph/socfb-Princeton12/socfb-Princeton12.mtx";
const std::string kProxyIp = "18.18.1.2";
constexpr static uint32_t kEntryObjPort = 9091;
constexpr static uint32_t kConcurrrency = 200;
constexpr static uint32_t kTextLen = 64;
constexpr static uint32_t kUrlLen = 64;
constexpr static uint32_t kMinNumPostsPerUser = 1;
constexpr static uint32_t kMaxNumPostsPerUser = 20;
constexpr static uint32_t kMaxNumMentionsPerText = 2;
constexpr static uint32_t kMaxNumUrlsPerText = 2;
constexpr static uint32_t kMaxNumMediasPerText = 2;
constexpr static bool kEnableCompose = false;

class ClientPtr {
public:
  ClientPtr(const std::string &ip) {
    socket.reset(new TSocket(ip, kEntryObjPort));
    transport.reset(new TFramedTransport(socket));
    protocol.reset(new TBinaryProtocol(transport));
    client.reset(new social_network::BackEndServiceClient(protocol));
    transport->open();
  }

  social_network::BackEndServiceClient *operator->() { return client.get(); }

private:
  std::shared_ptr<TTransport> socket;
  std::shared_ptr<TTransport> transport;
  std::shared_ptr<TProtocol> protocol;
  std::unique_ptr<social_network::BackEndServiceClient> client;
};

void do_work() {
  std::ifstream ifs(kFilePath);

  uint32_t num_nodes, num_edges;
  ifs >> num_nodes >> num_nodes >> num_edges;
  std::cout << "num_nodes = " << num_nodes << std::endl;
  std::cout << "num_edges = " << num_edges << std::endl;

  std::vector<ClientPtr> clients;
  for (uint32_t i = 0; i < kConcurrrency; i++) {
    clients.emplace_back(kProxyIp);
  }

  std::vector<nu::Future<void>> futures;
  for (uint32_t i = 0; i < num_nodes; i++) {
    futures.emplace_back(nu::async([&, user_id = i + 1] {
      std::string first_name = "first_name_" + std::to_string(user_id);
      std::string last_name = "last_name_" + std::to_string(user_id);
      std::string username = "username_" + std::to_string(user_id);
      std::string password = "password_" + std::to_string(user_id);
      clients[user_id % kConcurrrency]->RegisterUserWithId(
          first_name, last_name, username, password, user_id);
    }));
    if (futures.size() == kConcurrrency) {
      futures.clear();
      std::cout << "register " << i << std::endl;
    }
  }
  futures.clear();

  for (uint32_t i = 0; i < num_edges; i++) {
    int64_t user_id_x, user_id_y;
    ifs >> user_id_x >> user_id_y;
    futures.emplace_back(nu::async([&, i, user_id_x, user_id_y] {
      clients[i % kConcurrrency]->Follow(user_id_x, user_id_y);
      clients[i % kConcurrrency]->Follow(user_id_y, user_id_x);
    }));
    if (futures.size() == kConcurrrency) {
      futures.clear();
      std::cout << "follow " << i << std::endl;
    }
  }
  futures.clear();

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist_num_posts(kMinNumPostsPerUser,
                                                 kMaxNumPostsPerUser);
  std::uniform_int_distribution<> dist_num_mentions(0, kMaxNumMentionsPerText);
  std::uniform_int_distribution<> dist_num_urls(0, kMaxNumUrlsPerText);
  std::uniform_int_distribution<> dist_num_medias(0, kMaxNumMediasPerText);
  std::uniform_int_distribution<> dist_user_id(1, num_nodes);
  std::uniform_int_distribution<char> dist_chars('a', 'z');
  std::uniform_int_distribution<uint64_t> dist_u64(
      0, std::numeric_limits<uint64_t>::max());

  if constexpr (kEnableCompose) {
    for (uint32_t user_id = 1; user_id <= num_nodes; user_id++) {
      auto num_posts = dist_num_posts(gen);
      for (uint32_t pid = 0; pid < num_posts; pid++) {
        auto username = "username_" + std::to_string(user_id);

        std::string text = "";
        for (uint32_t i = 0; i < kTextLen; i++) {
          text += dist_chars(gen);
        }

        auto num_mentions = dist_num_mentions(gen);
        for (uint32_t i = 0; i < num_mentions; i++) {
          auto mentioned_id = dist_user_id(gen);
          text += " @username_" + std::to_string(mentioned_id);
        }

        auto num_urls = dist_num_urls(gen);
        for (uint32_t i = 0; i < num_urls; i++) {
          text += " http://";
          for (uint32_t j = 0; j < kUrlLen; j++) {
            text += dist_chars(gen);
          }
        }

        std::vector<int64_t> media_ids;
        std::vector<std::string> media_types;
        auto num_medias = dist_num_medias(gen);
        for (uint32_t i = 0; i < num_medias; i++) {
          media_ids.emplace_back(dist_u64(gen));
          media_types.emplace_back("png");
        }

        auto post_type = social_network::PostType::POST;

        clients[0]->ComposePost(username, user_id, text, media_ids, media_types,
                                post_type);
        std::cout << "compose " << user_id << " " << pid << std::endl;
      }
    }
  }

  std::cout << "done" << std::endl;
}

int main(int argc, char **argv) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = rt::RuntimeInit(std::string(argv[1]), [] { do_work(); });

  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}

