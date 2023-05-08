#pragma once

#include <fstream>
#include <nu/dis_executor.hpp>
#include <nu/pressure_handler.hpp>
#include <nu/runtime.hpp>
#include <nu/sealed_ds.hpp>
#include <nu/sharded_queue.hpp>
#include <nu/sharded_vector.hpp>
#include <opencv2/opencv.hpp>

#include "image.hpp"

namespace imagenet {

using Batch = std::vector<RawImage>;
using shard_vec_type = nu::ShardedVector<RawImage, std::false_type>;
using sealed_shard_vec_type = nu::SealedDS<shard_vec_type>;
using shard_queue_type = nu::ShardedQueue<Image, std::true_type>;

template <typename Elem>
class MockGPU;

class DataLoader {
 public:
  using GPU = MockGPU<Image>;
  using TraceVec = std::vector<std::vector<std::pair<uint64_t, uint64_t>>>;

  static constexpr uint32_t kMaxNumGPUs = 8;
  static constexpr auto kGPUIP = MAKE_IP_ADDR(18, 18, 1, 10);
  static constexpr uint64_t kNumScaleDownGPUs = kMaxNumGPUs / 2;
  static constexpr uint64_t kScaleUpDurationUs = nu::kOneMilliSecond * 500;
  static constexpr uint64_t kScaleDownDurationUs = nu::kOneMilliSecond * 500;

  DataLoader(std::string path);
  std::size_t size() const;
  ~DataLoader();
  uint64_t process_all();

 private:
  TraceVec run_gpus();
  void process_traces(TraceVec&& all_traces);

  shard_vec_type imgs_;
  shard_queue_type queue_;
  bool processed_ = false;
};

class BaselineDataLoader {
 public:
  BaselineDataLoader(std::string path, int nthreads);
  void process_all();

 private:
  void process(int tid);
  rt::TcpConn* dial_gpu_server();

  int nthreads_;
  std::vector<RawImage> imgs_;
};

}  // namespace imagenet
