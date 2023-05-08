extern "C" {
#include <runtime/timer.h>
}

#include <runtime.h>
#include <sync.h>

#include "nu/runtime.hpp"
#include "nu/commons.hpp"
#include "nu/ctrl_client.hpp"
#include "nu/resource_reporter.hpp"

namespace nu {

ResourceReporter::ResourceReporter() : done_(false) {
  th_ = rt::Thread([&] {
    set_resource_reporting_handler(thread_self());

    rt::Preempt p;
    rt::PreemptGuard g(&p);
    do {
      thread_park_and_preempt_enable();
      report_resource();
    } while (!rt::access_once(done_));
    set_resource_reporting_handler(nullptr);
  });
}

std::vector<std::pair<NodeIP, Resource>>
ResourceReporter::get_global_free_resources() {
  rt::ScopedLock lock(&spin_);

  return global_free_resources_;
}

ResourceReporter::~ResourceReporter() {
  done_ = true;
  barrier();
  th_.Join();
}

void ResourceReporter::report_resource() {
  Resource resource;
  resource.cores = std::min(rt::RuntimeGlobalIdleCores(),
                            rt::RuntimeMaxCores() - rt::RuntimeActiveCores());
  resource.mem_mbs = rt::RuntimeFreeMemMbs();
  auto global_free_resources =
      get_runtime()->controller_client()->report_free_resource(resource);
  {
    rt::ScopedLock lock(&spin_);

    global_free_resources_ = std::move(global_free_resources);
  }
  finish_resource_reporting();
}

}  // namespace nu
