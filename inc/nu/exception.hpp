#include <exception>
#include <stdexcept>

namespace nu {

struct OutOfMemory : public std::exception {
  const char *what() const throw() { return "Out of memory"; }
};

}  // namespace nu
