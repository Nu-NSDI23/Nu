#include "MediaService.h"

namespace social_network {

std::vector<Media>
MediaService::ComposeMedia(std::vector<std::string> media_types,
                           std::vector<int64_t> media_ids) {
  BUG_ON(media_types.size() != media_ids.size());

  std::vector<Media> medias;
  for (int i = 0; i < media_ids.size(); ++i) {
    Media media;
    media.media_id = media_ids[i];
    media.media_type = media_types[i];
    medias.emplace_back(media);
  }
  return medias;
}
} // namespace social_network
