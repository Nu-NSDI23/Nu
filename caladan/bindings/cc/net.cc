#include "net.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

size_t SumIOV(std::span<const iovec> iov) {
  size_t len = 0;
  for (const iovec &e : iov) {
    len += e.iov_len;
  }
  return len;
}

std::span<iovec> PullIOV(std::span<iovec> iov, size_t n) {
  for (auto it = iov.begin(); it < iov.end(); ++it) {
    if (n < it->iov_len) {
      (*it).iov_base = reinterpret_cast<char *>(it->iov_base) + n;
      (*it).iov_len -= n;
      return {it, iov.end()};
    }
    n -= it->iov_len;
  }

  assert(n == 0);
  return {};
}

}  // namespace

namespace rt {

ssize_t TcpConn::WritevFullRaw(std::span<const iovec> iov, bool nt, bool poll) {
  // first try to send without copying the vector
  ssize_t n = __tcp_writev(c_, iov.data(), iov.size(), nt, poll);
  if (n < 0) return n;
  assert(n > 0);

  // sum total length and check if everything was transfered
  size_t total = SumIOV(iov);
  if (static_cast<size_t>(n) == total) return n;

  // partial transfer occurred, send the rest
  size_t len = n;
  iovec v[iov.size()];
  std::copy(iov.begin(), iov.end(), v);
  std::span<iovec> s(v, iov.size());
  while (true) {
    s = PullIOV(s, n);
    if (s.empty()) break;
    n = __tcp_writev(c_, s.data(), s.size(), nt, poll);
    if (n < 0) return n;
    assert(n > 0);
    len += n;
  }

  assert(len == total);
  return len;
}

ssize_t TcpConn::ReadvFullRaw(std::span<const iovec> iov, bool nt, bool poll) {
  // first try to receive without copying the vector
  ssize_t n = __tcp_readv(c_, iov.data(), iov.size(), nt, poll);
  if (n <= 0) return n;

  // sum total length and check if everything was transfered
  size_t total = SumIOV(iov);
  if (static_cast<size_t>(n) == total) return n;

  // partial transfer occurred, receive the rest
  size_t len = n;
  iovec v[iov.size()];
  std::copy(iov.begin(), iov.end(), v);
  std::span<iovec> s(v, iov.size());
  while (true) {
    s = PullIOV(s, n);
    if (s.empty()) break;
    n = __tcp_readv(c_, s.data(), s.size(), nt, poll);
    if (n <= 0) return n;
    len += n;
  }

  assert(len == total);
  return len;
}

}  // namespace rt
