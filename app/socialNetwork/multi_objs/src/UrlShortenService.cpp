#include "UrlShortenService.h"

namespace social_network {

UrlShortenService::UrlShortenService()
    : _short_to_extended_map(
          nu::make_dis_hash_table<std::string, std::string, StrHasher>(
              kDefaultHashTablePowerNumShards)) {}

std::string UrlShortenService::GenRandomStr(int length) {
  const char char_map[] = "abcdefghijklmnopqrstuvwxyzABCDEF"
                          "GHIJKLMNOPQRSTUVWXYZ0123456789";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(
      0, static_cast<int>(sizeof(char_map) - 2));
  std::string return_str;
  for (int i = 0; i < length; ++i) {
    return_str.append(1, char_map[dist(gen)]);
  }
  return return_str;
}

std::vector<Url> UrlShortenService::ComposeUrls(std::vector<std::string> urls) {
  std::vector<Url> target_urls;

  for (auto &url : urls) {
    Url target_url;
    target_url.expanded_url = std::move(url);
    target_url.shortened_url = HOSTNAME + GenRandomStr(10);
    target_urls.emplace_back(std::move(target_url));
  }

  std::vector<nu::Future<void>> put_futures;
  for (auto &target_url : target_urls) {
    put_futures.emplace_back(_short_to_extended_map.put_async(
        target_url.shortened_url, target_url.expanded_url));
  }
  for (auto &put_future : put_futures) {
    put_future.get();
  }
  return target_urls;
}

void UrlShortenService::RemoveUrls(std::vector<std::string> shortened_urls) {
  std::vector<nu::Future<bool>> futures;
  for (const auto &url : shortened_urls) {
    futures.emplace_back(_short_to_extended_map.remove_async(url));
  }
}

std::vector<std::string>
UrlShortenService::GetExtendedUrls(std::vector<std::string> shortened_urls) {
  std::vector<nu::Future<std::optional<std::string>>>
      extended_url_optional_futures;
  for (auto &shortened_url : shortened_urls) {
    extended_url_optional_futures.emplace_back(
        _short_to_extended_map.get_async(std::move(shortened_url)));
  }

  std::vector<std::string> extended_urls;
  for (auto &extended_url_optional_future : extended_url_optional_futures) {
    auto &extended_urls_optional = extended_url_optional_future.get();
    BUG_ON(!extended_urls_optional);
    extended_urls.emplace_back(std::move(*extended_urls_optional));
  }
  return extended_urls;
}
} // namespace social_network
