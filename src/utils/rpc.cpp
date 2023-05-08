#include <type_traits>

extern "C" {
#include <base/log.h>
#include <runtime/timer.h>
}
#include <runtime.h>

#include "nu/utils/rpc.hpp"
#include "nu/runtime.hpp"

namespace nu {

namespace {

// Command types for the RPC protocol.
enum rpc_cmd : unsigned int {
  call = 0,
  update,
};

// Binary header format for requests sent by client.
struct rpc_req_hdr {
  rpc_cmd cmd;          // the command type
  unsigned int demand;  // number of RPCs waiting to be sent and inflight
  std::size_t len;      // the length of this RPC request
  std::size_t completion_data;  // an opaque token to complete the RPC
};

constexpr rpc_req_hdr MakeCallRequest(unsigned int demand, std::size_t len,
                                      std::size_t completion_data) {
  return rpc_req_hdr{rpc_cmd::call, demand, len, completion_data};
}

constexpr rpc_req_hdr MakeUpdateRequest(unsigned int demand) {
  return rpc_req_hdr{rpc_cmd::update, demand, 0, 0};
}

// Binary header format for responses sent by server.
struct rpc_resp_hdr {
  rpc_cmd cmd;                  // the command type
  unsigned int credits;         // the number of credits available
  ssize_t len;                  // the length of this RPC response,
                                // < 0 indicates an error
  std::size_t completion_data;  // an opaque token to complete the RPC
};

constexpr rpc_resp_hdr MakeCallResponse(unsigned int credits, ssize_t len,
                                        std::size_t completion_data) {
  return rpc_resp_hdr{rpc_cmd::call, credits, len, completion_data};
}

constexpr rpc_resp_hdr MakeUpdateResponse(unsigned int credits) {
  return rpc_resp_hdr{rpc_cmd::update, credits, 0, 0};
}

}  // namespace

namespace rpc_internal {

void RPCCompletion::Poll() const {
  while (rt::access_once(poll_)) {
    get_runtime()->caladan()->unblock_and_relax();
  }
}

void RPCCompletion::Done(ssize_t len, rt::TcpConn *c) {
  if (unlikely(len < 0)) {
    rc_ = static_cast<RPCReturnCode>(len);
  } else {
    rc_ = kOk;

    if (callback_) {
      callback_(len, c);
    } else if (len) {
      auto buf = std::make_unique_for_overwrite<std::byte[]>(len);
      auto ret = c->ReadFull(buf.get(), len);
      if (unlikely(ret <= 0)) {
        log_err("rpc: ReadFull failed, err = %ld", ret);
      }
      auto span = std::span<const std::byte>(buf.get(), len);
      return_buf_->Reset(span, [buf = std::move(buf)] {});
    }
  }

  poll_ = false;
  w_.Wake();
}

RPCServerWorker::RPCServerWorker(std::unique_ptr<rt::TcpConn> c,
                                 nu::RPCHandler &handler, Counter &counter)
    : c_(std::move(c)),
      handler_(handler),
      close_(false),
      counter_(counter),
      sender_([this] { SendWorker(); }),
      receiver_([this] { ReceiveWorker(); }) {}

RPCServerWorker::~RPCServerWorker() {
  {
    rt::SpinGuard guard(&lock_);
    close_ = true;
    wake_sender_.Wake();
  }

  sender_.Join();
  c_->Shutdown(SHUT_RDWR);
  receiver_.Join();
}

void RPCServerWorker::SendWorker() {
  std::vector<completion> completions;
  std::vector<iovec> iovecs;
  std::vector<rpc_resp_hdr> hdrs;

  while (true) {
    {
      // wait for an actionable state.
      rt::SpinGuard guard(&lock_);
      while (completions_.empty() && !close_) guard.Park(&wake_sender_);

      // gather all queued completions.
      std::move(completions_.begin(), completions_.end(),
                std::back_inserter(completions));
      completions_.clear();
    }
    // Check if the connection is closed.
    if (unlikely(close_ && completions.empty())) break;
    // process each of the requests.
    iovecs.clear();
    hdrs.clear();
    hdrs.reserve(completions.size());
    for (const auto &c : completions) {
      auto span = c.buf.get_buf();
      hdrs.emplace_back(MakeCallResponse(
          credits_, c.rc == kOk ? span.size_bytes() : c.rc, c.completion_data));
      iovecs.emplace_back(&hdrs.back(), sizeof(decltype(hdrs)::value_type));
      if (span.size_bytes() == 0) continue;
      iovecs.emplace_back(const_cast<std::byte *>(span.data()),
                          span.size_bytes());
    }

    // send data on the wire.
    ssize_t ret = c_->WritevFull(std::span<const iovec>(iovecs));
    if (unlikely(ret <= 0)) {
      log_err("rpc: WritevFull failed, err = %ld", ret);
      return;
    }
    completions.clear();
  }
  if (WARN_ON(c_->Shutdown(SHUT_WR))) c_->Abort();
}

void RPCServerWorker::ReceiveWorker() {
  while (true) {
    // Read the request header.
    rpc_req_hdr hdr;
    ssize_t ret = c_->ReadFull(&hdr, sizeof(hdr));
    if (unlikely(ret == 0)) break;
    if (unlikely(ret < 0)) {
      log_err("rpc: ReadFull failed, err = %ld", ret);
      break;
    }

    // Parse the request header.
    std::size_t completion_data = hdr.completion_data;
    demand_ = hdr.demand;
    if (hdr.cmd != rpc_cmd::call) continue;

    // Spawn a handler with no argument data provided.
    if (hdr.len == 0) {
      counter_.inc();
      // TODO: avoid dynamic memory allocation.
      rt::Spawn([this, completion_data]() {
        auto returner = RPCReturner(this, completion_data);
        handler_(std::span<std::byte>{}, &returner);
        counter_.dec();
      });
      continue;
    }

    // Allocate and fill a buffer with the argument data.
    auto buf = std::make_unique_for_overwrite<std::byte[]>(hdr.len);
    ret = c_->ReadFull(buf.get(), hdr.len);
    if (unlikely(ret == 0)) break;
    if (unlikely(ret < 0)) {
      log_err("rpc: ReadFull failed, err = %ld", ret);
      return;
    }

    // Spawn a handler with argument data provided.
    counter_.inc();
    // TODO: avoid dynamic memory allocation.
    rt::Spawn(
        [this, completion_data, b = std::move(buf), len = hdr.len]() mutable {
          auto returner = RPCReturner(this, completion_data);
          handler_(std::span<std::byte>{b.get(), len}, &returner);
          counter_.dec();
        });
  }

  // Wake the sender to close the connection.
  {
    rt::SpinGuard guard(&lock_);
    close_ = true;
    wake_sender_.Wake();
  }
}

RPCFlow::~RPCFlow() {
  {
    rt::SpinGuard guard(&lock_);
    close_ = true;
    wake_sender_.Wake();
  }
  sender_.Join();
  receiver_.Join();
}

inline bool RPCFlow::EnoughBatching() {
  return reqs_.size() >= kReqBatchSize ||
         microtime() - last_sent_us_ >= kBatchTimeoutUs;
}

void RPCFlow::SendWorker() {
  std::vector<req_ctx> reqs;
  std::vector<iovec> iovecs;
  std::vector<rpc_req_hdr> hdrs;

  while (true) {
    unsigned int demand, inflight;
    bool close;

    // adapative batching.
    while (kEnableAdaptiveBatching && !EnoughBatching()) {
      rt::Yield();
    }

    {
      // wait for an actionable state.
      rt::SpinGuard guard(&lock_);
      inflight = sent_count_ - recv_count_;
      while ((reqs_.empty() || inflight >= credits_) &&
             !(close_ && reqs_.empty())) {
        guard.Park(&wake_sender_);
        inflight = sent_count_ - recv_count_;
      }

      // gather queued requests up to the credit limit.
      last_sent_us_ = microtime();
      while (!reqs_.empty() && inflight < credits_) {
        reqs.emplace_back(reqs_.front());
        reqs_.pop();
        inflight++;
      }
      sent_count_ += reqs.size();
      close = close_ && reqs_.empty();
      demand = inflight;
    }

    // Check if it is time to close the connection.
    if (unlikely(close)) break;

    // construct a scatter-gather list for all the pending requests.
    iovecs.clear();
    hdrs.clear();
    hdrs.reserve(reqs.size());
    for (const auto &r : reqs) {
      auto &span = r.payload;
      hdrs.emplace_back(
          MakeCallRequest(demand, span.size_bytes(),
                          reinterpret_cast<std::size_t>(r.completion)));
      iovecs.emplace_back(&hdrs.back(), sizeof(decltype(hdrs)::value_type));
      if (span.size_bytes() == 0) continue;
      iovecs.emplace_back(const_cast<std::byte *>(span.data()),
                          span.size_bytes());
    }

    // send data on the wire.
    ssize_t ret = c_->WritevFull(std::span<const iovec>(iovecs));
    if (unlikely(ret <= 0)) {
      log_err("rpc: WritevFull failed, err = %ld", ret);
      return;
    }
    reqs.clear();
  }

  // send FIN on the wire.
  if (WARN_ON(c_->Shutdown(SHUT_WR))) c_->Abort();
}

void RPCFlow::ReceiveWorker() {
  while (true) {
    // Read the response header.
    rpc_resp_hdr hdr;
    ssize_t ret = c_->ReadFull(&hdr, sizeof(hdr));
    if (unlikely(ret <= 0)) {
      log_err("rpc: ReadFull failed, err = %ld", ret);
      return;
    }

    // Check if we should wake the sender.
    {
      rt::SpinGuard guard(&lock_);
      unsigned int inflight = sent_count_ - ++recv_count_;
      // credits_ = hdr.credits;
      if (credits_ > inflight && !reqs_.empty()) wake_sender_.Wake();
    }

    if (hdr.cmd != rpc_cmd::call) continue;

    // Check if there is no return data.
    auto *completion = reinterpret_cast<RPCCompletion *>(hdr.completion_data);
    completion->Done(hdr.len, c_.get());
  }
}

std::unique_ptr<RPCFlow> RPCFlow::New(unsigned int cpu_affinity,
                                      netaddr raddr) {
  std::unique_ptr<rt::TcpConn> c(
      rt::TcpConn::DialAffinity(cpu_affinity, raddr));
  BUG_ON(!c);
  std::unique_ptr<RPCFlow> f = std::make_unique<RPCFlow>(std::move(c));
  f->sender_ = rt::Thread([f = f.get()] { f->SendWorker(); });
  f->receiver_ = rt::Thread([f = f.get()] { f->ReceiveWorker(); });
  return f;
}

}  // namespace rpc_internal

std::unique_ptr<RPCClient> RPCClient::Dial(netaddr raddr) {
  std::vector<std::unique_ptr<RPCFlow>> v;
  for (unsigned int i = 0; i < rt::RuntimeMaxCores(); ++i) {
    v.emplace_back(RPCFlow::New(i, raddr));
  }
  return std::unique_ptr<RPCClient>(new RPCClient(std::move(v), raddr));
}

RPCServerListener::RPCServerListener(uint16_t port, RPCHandler &&handler)
    : handler_(std::move(handler)) {
  q_.reset(rt::TcpQueue::Listen({0, port}, 4096));
  BUG_ON(!q_);

  listener_ = rt::Thread([&]() mutable {
    rt::TcpConn *c;
    while ((c = q_->Accept())) {
      workers_.emplace_back(new rpc_internal::RPCServerWorker(
          std::unique_ptr<rt::TcpConn>(c), handler_, counter_));
    }
  });
}

RPCServerListener::~RPCServerListener() {
  q_->Shutdown();
  listener_.Join();

  while (counter_.get()) {
    rt::Yield();
  }
}

}  // namespace nu
