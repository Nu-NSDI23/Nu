#include "initializer.hpp"

#include <fstream>
#include <vector>

namespace social_network {

Initializer::Initializer(nu::Proclet<BackEndService> backend)
    : backend_(std::move(backend)) {}

void Initializer::init() {
  std::cout << "init graph..." << std::endl;
  std::ifstream ifs(kFilePath);

  uint32_t num_nodes, num_edges;
  ifs >> num_nodes >> num_nodes >> num_edges;
  std::cout << "num_nodes = " << num_nodes << std::endl;
  std::cout << "num_edges = " << num_edges << std::endl;

  std::vector<nu::Future<void>> futures;
  for (uint32_t i = 0; i < num_nodes; i++) {
    auto user_id = i + 1;
    std::string first_name = "first_name_" + std::to_string(user_id);
    std::string last_name = "last_name_" + std::to_string(user_id);
    std::string username = "username_" + std::to_string(user_id);
    std::string password = "password_" + std::to_string(user_id);
    futures.emplace_back(backend_.run_async(&BackEndService::RegisterUserWithId,
                                            first_name, last_name, username,
                                            password, user_id));
    if (futures.size() > kConcurrency) {
      futures.clear();
    }
    std::cout << "register " << i << std::endl;
  }
  futures.clear();

  for (uint32_t i = 0; i < num_edges; i++) {
    int64_t user_id_x, user_id_y;
    ifs >> user_id_x >> user_id_y;
    futures.emplace_back(
        backend_.run_async(&BackEndService::Follow, user_id_x, user_id_y));
    futures.emplace_back(
        backend_.run_async(&BackEndService::Follow, user_id_y, user_id_x));
    if (futures.size() > kConcurrency) {
      futures.clear();
    }
    std::cout << "follow " << i << std::endl;
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
        futures.emplace_back(
            backend_.run_async(&BackEndService::ComposePost, username, user_id,
                               text, media_ids, media_types, post_type));
        if (futures.size() > kConcurrency) {
          futures.clear();
        }
        std::cout << "compose " << user_id << " " << pid << std::endl;
      }
    }
    futures.clear();
  }

  std::cout << "done" << std::endl;
}
}  // namespace social_network
