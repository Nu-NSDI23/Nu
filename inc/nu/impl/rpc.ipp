namespace nu {

namespace rpc_internal {

class RPCServerWorker {
 public:
  RPCServerWorker(std::unique_ptr<rt::TcpConn> c, nu::RPCHandler &handler,
                  Counter &counter);
  ~RPCServerWorker();

  // Sends the return results of an RPC.
  void Return(RPCReturnCode rc, RPCReturnBuffer &&buf,
              std::size_t completion_data);

 private:
  // Internal worker threads for sending and receiving.
  void SendWorker();
  void ReceiveWorker();

  struct completion {
    RPCReturnCode rc;
    RPCReturnBuffer buf;
    std::size_t completion_data;
  };

  rt::Spin lock_;
  std::unique_ptr<rt::TcpConn> c_;
  nu::RPCHandler &handler_;
  bool close_;
  Counter &counter_;
  rt::ThreadWaker wake_sender_;
  std::vector<completion> completions_;
  float credits_;
  unsigned int demand_;
  rt::Thread sender_;
  rt::Thread receiver_;
};

inline void RPCServerWorker::Return(RPCReturnCode rc, RPCReturnBuffer &&buf,
                                    std::size_t completion_data) {
  rt::SpinGuard guard(&lock_);
  completions_.emplace_back(rc, std::move(buf), completion_data);
  wake_sender_.Wake();
}

inline void RPCFlow::Call(std::span<const std::byte> src, RPCCompletion *c) {
  rt::SpinGuard guard(&lock_);
  reqs_.emplace(req_ctx{src, c});
  if (sent_count_ - recv_count_ < credits_) wake_sender_.Wake();
}

}  // namespace rpc_internal

inline RPCReturner::RPCReturner(void *rpc_server, std::size_t completion_data)
    : rpc_server_(rpc_server), completion_data_(completion_data) {}

inline void RPCReturner::Return(RPCReturnCode rc,
                                std::span<const std::byte> buf,
                                std::move_only_function<void()> deleter_fn) {
  auto rpc_server =
      reinterpret_cast<rpc_internal::RPCServerWorker *>(rpc_server_);
  rpc_server->Return(rc, RPCReturnBuffer(buf, std::move(deleter_fn)),
                     completion_data_);
}

inline void RPCReturner::Return(RPCReturnCode rc) {
  auto rpc_server =
      reinterpret_cast<rpc_internal::RPCServerWorker *>(rpc_server_);
  rpc_server->Return(rc, RPCReturnBuffer(), completion_data_);
}

inline RPCReturnCode RPCClient::Call(std::span<const std::byte> args,
                                     RPCCallback &&callback) {
  RPCCompletion completion(std::move(callback));
  {
    rt::Preempt p;
    if (!p.IsHeld()) {
      rt::PreemptGuardAndPark guard(&p);
      flows_[p.get_cpu()]->Call(args, &completion);
    } else {
      flows_[p.get_cpu()]->Call(args, &completion);
    }
  }
  return completion.get_return_code();
}

inline RPCReturnCode RPCClient::Call(std::span<const std::byte> args,
                                     RPCReturnBuffer *return_buf) {
  RPCCompletion completion(return_buf);
  {
    rt::Preempt p;
    if (!p.IsHeld()) {
      rt::PreemptGuardAndPark guard(&p);
      flows_[p.get_cpu()]->Call(args, &completion);
    } else {
      flows_[p.get_cpu()]->Call(args, &completion);
    }
  }
  return completion.get_return_code();
}

}  // namespace nu
