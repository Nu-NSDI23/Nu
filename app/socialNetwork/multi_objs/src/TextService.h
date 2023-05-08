#pragma once

#include <future>
#include <iostream>
#include <nu/rem_obj.hpp>
#include <regex>
#include <string>

#include "UrlShortenService.h"
#include "UserMentionService.h"

namespace social_network {

class TextService {
public:
  TextService(nu::RemObj<UrlShortenService>::Cap url_shorten_service_cap,
              nu::RemObj<UserMentionService>::Cap user_mention_service_cap);
  TextServiceReturn ComposeText(std::string);

private:
  nu::RemObj<UrlShortenService> _url_shorten_service_obj;
  nu::RemObj<UserMentionService> _user_mention_service_obj;
};

}  // namespace social_network
