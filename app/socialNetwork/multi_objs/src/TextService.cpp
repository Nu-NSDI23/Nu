#include "TextService.h"

namespace social_network {

TextService::TextService(nu::Proclet<UrlShortenService> url_shorten_service,
                         nu::Proclet<UserMentionService> user_mention_service)
    : _url_shorten_service(std::move(url_shorten_service)),
      _user_mention_service(std::move(user_mention_service)) {}

TextServiceReturn TextService::ComposeText(std::string text) {
  auto http_pattern = "(http://|https://)([a-zA-Z0-9_!~*'().&=+$%-]+)";
  auto mention_pattern = "@[a-zA-Z0-9-_]+";

  std::vector<std::string> urls;
  std::smatch m;
  std::regex e(http_pattern);
  auto s = text;
  while (std::regex_search(s, m, e)) {
    auto url = m.str();
    urls.emplace_back(url);
    s = m.suffix().str();
  }
  auto target_urls_future =
      _url_shorten_service.run_async(&UrlShortenService::ComposeUrls, urls);

  std::vector<std::string> mention_usernames;
  e = mention_pattern;
  s = text;
  while (std::regex_search(s, m, e)) {
    auto user_mention = m.str();
    user_mention = user_mention.substr(1, user_mention.length());
    mention_usernames.emplace_back(user_mention);
    s = m.suffix().str();
  }
  auto user_mentions_future = _user_mention_service.run_async(
      &UserMentionService::ComposeUserMentions, mention_usernames);

  auto &target_urls = target_urls_future.get();
  std::string updated_text;
  if (!urls.empty()) {
    s = text;
    e = http_pattern;
    int idx = 0;
    while (std::regex_search(s, m, e)) {
      auto url = m.str();
      urls.emplace_back(url);
      updated_text += m.prefix().str() + target_urls[idx].shortened_url;
      s = m.suffix().str();
      idx++;
    }
    updated_text += s;
  } else {
    updated_text = text;
  }

  auto &user_mentions = user_mentions_future.get();
  TextServiceReturn text_service_return;
  text_service_return.user_mentions = std::move(user_mentions);
  text_service_return.text = std::move(updated_text);
  text_service_return.urls = std::move(target_urls);
  return text_service_return;
}
} // namespace social_network
