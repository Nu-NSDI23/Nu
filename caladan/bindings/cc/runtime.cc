#include "runtime.h"
#include "thread.h"

namespace rt {

// initializes the runtime
int RuntimeInit(std::string cfg_path, std::function<void()> main_func) {
  auto *func_ptr = new std::move_only_function<void()>(main_func);
  return runtime_init(cfg_path.c_str(), thread_internal::ThreadTrampoline,
                      reinterpret_cast<void *>(func_ptr));
}

}  // namespace rt
