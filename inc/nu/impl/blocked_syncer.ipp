extern "C" {
#include <base/assert.h>
}

namespace nu {

inline void BlockedSyncer::add(void *syncer, Type type) {
  sync_map_.put(syncer, type);
}

inline void BlockedSyncer::remove(void *syncer) {
  BUG_ON(!sync_map_.remove(syncer));
}

inline std::vector<std::pair<void *, BlockedSyncer::Type>>
BlockedSyncer::get_all() {
  return sync_map_.get_all_pairs();
}

}  // namespace nu
