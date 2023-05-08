#include <runtime.h>

namespace nu {

inline bool PressureHandler::has_cpu_pressure() {
  return rt::RuntimeCpuPressure();
}

inline bool PressureHandler::has_mem_pressure() {
  return rt::RuntimeToReleaseMemMbs();
}

inline bool PressureHandler::has_pressure() {
  return has_cpu_pressure() || has_mem_pressure();
}

inline bool PressureHandler::has_real_pressure() {
  return has_pressure() && !mock_;
}

}  // namespace nu
