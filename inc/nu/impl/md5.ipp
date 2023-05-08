#include <string.h>

namespace nu {

inline bool MD5Val::operator==(const MD5Val &o) const {
  return __builtin_strncmp(reinterpret_cast<const char *>(data),
                           reinterpret_cast<const char *>(o.data),
                           MD5_DIGEST_LENGTH) == 0;
}

inline bool MD5Val::operator!=(const MD5Val &o) const {
  return !this->operator==(o);
}

inline MD5Val get_self_md5() { return get_md5("/proc/self/exe"); }

}  // namespace nu
