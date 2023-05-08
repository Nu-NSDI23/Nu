#include <openssl/md5.h>

#include <string>

namespace nu {

struct MD5Val {
  unsigned char data[MD5_DIGEST_LENGTH];

  bool operator==(const MD5Val &o) const;
  bool operator!=(const MD5Val &o) const;
};

MD5Val get_md5(std::string file_name);
MD5Val get_self_md5();
}  // namespace nu

#include "nu/impl/md5.ipp"
