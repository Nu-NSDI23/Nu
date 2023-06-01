#pragma once

#include <future>
#include <iostream>
#include <nu/proclet.hpp>
#include <regex>
#include <string>

#include "UrlShortenService.h"
#include "UserMentionService.h"

namespace social_network {

class TextService {
public:
  TextService(nu::Proclet<UrlShortenService> url_shorten_service,
              nu::Proclet<UserMentionService> user_mention_service);
  TextServiceReturn ComposeText(std::string);

private:
  nu::Proclet<UrlShortenService> _url_shorten_service;
  nu::Proclet<UserMentionService> _user_mention_service;
};

}  // namespace social_network
