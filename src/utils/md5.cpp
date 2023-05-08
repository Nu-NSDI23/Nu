#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/evp.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>

extern "C" {
#include <base/assert.h>
#include <base/stddef.h>
}

#include "nu/utils/md5.hpp"

namespace nu {

inline unsigned long get_size_by_fd(int fd) {
  struct stat statbuf;
  if (fstat(fd, &statbuf) < 0) {
    exit(-1);
  }
  return statbuf.st_size;
}

MD5Val get_md5(std::string file_name) {
  auto fd = open(file_name.c_str(), O_RDONLY);
  BUG_ON(fd < 0);
  auto file_size = get_size_by_fd(fd);
  auto file_buf = mmap(0, file_size, PROT_READ, MAP_SHARED, fd, 0);
  BUG_ON(file_buf == MAP_FAILED);

  MD5Val md5_val;
  const EVP_MD *algo = EVP_md5();
  auto context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);

  EVP_DigestInit_ex(context.get(), algo, nullptr);
  EVP_DigestUpdate(context.get(), file_buf, file_size);
  EVP_DigestFinal_ex(context.get(), md5_val.data, nullptr);

  BUG_ON(munmap(file_buf, file_size) == -1);
  return md5_val;
}

}  // namespace nu
