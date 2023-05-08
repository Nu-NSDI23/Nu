#include <cstdint>
#include <memory>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <utility>

extern "C" {
#include <base/assert.h>
}
#include <thread.h>

#include "nu/commons.hpp"
#include "nu/runtime.hpp"
#include "nu/ctrl_client.hpp"
#include "nu/migrator.hpp"
#include "nu/proclet_mgr.hpp"
#include "nu/proclet_server.hpp"

constexpr static bool kEnableLogging = false;
constexpr static uint32_t kPrintLoggingIntervalUs = 200 * 1000;

namespace nu {

ProcletServer::ProcletServer() {
  if constexpr (kEnableLogging) {
    trace_logger_.enable_print(kPrintLoggingIntervalUs);
  }
}

ProcletServer::~ProcletServer() {
  while (unlikely(ref_cnt_.get())) {
    rt::Yield();
  }
}

void ProcletServer::parse_and_run_handler(std::span<std::byte> args,
                                          RPCReturner *returner) {
  ref_cnt_.inc();

  auto *ia_sstream = get_runtime()->archive_pool()->get_ia_sstream();
  auto &[args_ss, ia] = *ia_sstream;
  args_ss.span({reinterpret_cast<char *>(args.data()), args.size()});

  GenericHandler handler;
  ia_sstream->ia >> handler;

  if constexpr (kEnableLogging) {
    trace_logger_.add_trace([&] { handler(ia_sstream, returner); });
  } else {
    handler(ia_sstream, returner);
  }

  get_runtime()->archive_pool()->put_ia_sstream(ia_sstream);

  ref_cnt_.dec();
}

void ProcletServer::dec_ref_cnt() { ref_cnt_.dec(); }

}  // namespace nu
