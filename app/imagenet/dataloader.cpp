#include "dataloader.hpp"

#include <runtime.h>
#include <thread.h>

#include <cereal/archives/binary.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

#include "baseline_gpu.hpp"
#include "gpu.hpp"

using directory_iterator = std::filesystem::recursive_directory_iterator;
using namespace std::chrono;
using namespace imagenet;

DataLoader::DataLoader(std::string path)
    : imgs_{nu::make_sharded_vector<RawImage, std::false_type>()},
      queue_{nu::make_sharded_queue<Image, std::true_type>()} {
  int image_count = 0;
  for (const auto &file_ : directory_iterator(path)) {
    if (file_.is_regular_file()) {
      const auto fname = file_.path().string();
      RawImage image(fname);
      imgs_.push_back(image);
      image_count++;
    }
  }
  std::cout << "DataLoader: " << image_count << " images loaded" << std::endl;
}

std::size_t DataLoader::size() const { return imgs_.size(); }

DataLoader::~DataLoader() { cv::cleanup(); }

uint64_t DataLoader::process_all() {
  BUG_ON(processed_);

  auto gpu_orchestrator = nu::async([&] { return run_gpus(); });

  auto sealed_imgs = nu::to_sealed_ds(std::move(imgs_));
  auto imgs_range = nu::make_contiguous_ds_range(sealed_imgs);

  auto start = high_resolution_clock::now();
  auto producers = nu::make_distributed_executor(
      +[](decltype(imgs_range) &imgs_range, decltype(queue_) queue) {
        while (true) {
          auto img = imgs_range.pop();
          if (!img) {
            break;
          }
          auto processed = kernel(std::move(*img));
          queue.push(std::move(processed));
        }
      },
      imgs_range, queue_);
  producers.get();
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(end - start);

  processed_ = true;
  barrier();

  auto all_traces = gpu_orchestrator.get();
  process_traces(std::move(all_traces));

  imgs_ = nu::to_unsealed_ds(std::move(sealed_imgs));

  return duration.count();
}

DataLoader::TraceVec DataLoader::run_gpus() {
  auto gpu = nu::make_proclet<GPU>(std::forward_as_tuple(queue_, kNumGPUs),
                                   true, std::nullopt, kGPUIP);

  while (!load_acquire(&processed_)) {
    gpu.run(&GPU::set_num_gpus, kNumScaleDownGPUs);
    nu::Time::sleep(kScaleDownDurationUs);

    gpu.run(&GPU::set_num_gpus, kNumGPUs);
    nu::Time::sleep(kScaleUpDurationUs);
  }

  return gpu.run(&GPU::drain_and_stop);
}

void DataLoader::process_traces(TraceVec &&all_traces) {
  auto min_us = std::numeric_limits<uint64_t>::max();
  auto max_us = std::numeric_limits<uint64_t>::min();

  for (auto &traces : all_traces) {
    min_us = std::min(min_us, traces.front().first);
    max_us = std::max(max_us, traces.back().second);
  }

  constexpr uint32_t kMonitorIntervalUs = 10 * nu::kOneMilliSecond;

  std::vector<std::pair<uint64_t, double>> total_utilizations;
  for (auto i = min_us; i < max_us; i += kMonitorIntervalUs) {
    total_utilizations.emplace_back(i, 0.0);
  }

  for (auto &traces : all_traces) {
    auto trace_it = traces.begin();
    for (auto &utilization : total_utilizations) {
      auto left_us = utilization.first;
      auto right_us = left_us + kMonitorIntervalUs;
      uint64_t active_us = 0;

      while (trace_it != traces.end() && right_us > trace_it->first) {
        if (trace_it->second > right_us) {
          active_us += right_us - trace_it->first;
          trace_it->first = right_us;
          break;
        }
        active_us += trace_it->second - trace_it->first;
        trace_it++;
      }

      utilization.second += static_cast<double>(active_us) / kMonitorIntervalUs;
    }
  }

  for (auto &[us, utilization] : total_utilizations) {
    printf("%lld %lf\n", (long long)us, utilization);
  }
}

BaselineDataLoader::BaselineDataLoader(std::string path, int nthreads)
    : nthreads_(nthreads) {
  int i = 0;
  for (const auto &file_ : directory_iterator(path)) {
    if (file_.is_regular_file()) {
      const auto fname = file_.path().string();
      RawImage image(fname);
      imgs_.push_back(image);
      i++;
    }
  }
  std::cout << "BaselineDataLoader: " << i << " images loaded" << std::endl;
}

rt::TcpConn *BaselineDataLoader::dial_gpu_server() {
  netaddr laddr{.ip = 0, .port = 0};
  netaddr raddr{.ip = kBaselineGPUIP, .port = kBaselineGPUPort};
  auto *conn = rt::TcpConn::Dial(laddr, raddr);
  BUG_ON(!conn);
  return conn;
}

void BaselineDataLoader::process(int tid) {
  auto *conn = dial_gpu_server();
  std::stringstream ss;
  cereal::BinaryOutputArchive oa(ss);

  auto num_imgs_per_thread = (imgs_.size() - 1) / nthreads_ + 1;
  auto start_idx = num_imgs_per_thread * tid;
  auto end_idx = std::min(imgs_.size(), start_idx + num_imgs_per_thread);

  for (size_t i = start_idx; i < end_idx; i++) {
    auto processed = kernel(imgs_[i]);
    oa << processed;
    uint64_t size = ss.tellp();
    auto *data = ss.view().data();
    const iovec iovecs[] = {{&size, sizeof(size)},
                            {const_cast<char *>(data), size}};
    ss.seekp(0);
    BUG_ON(conn->WritevFull(std::span(iovecs)) < 0);
    bool ack;
    BUG_ON(conn->ReadFull(&ack, sizeof(ack)) <= 0);
  }
}

void BaselineDataLoader::process_all() {
  auto *main_conn = dial_gpu_server();

  std::vector<rt::Thread> threads;
  for (int i = 0; i < nthreads_; i++) {
    threads.emplace_back([tid = i, this] { process(tid); });
  }
  for (auto &thread : threads) {
    thread.Join();
  }

  bool exit;
  BUG_ON(main_conn->WriteFull(&exit, sizeof(exit)) < 0);
  bool ack;
  BUG_ON(main_conn->ReadFull(&ack, sizeof(ack)) <= 0);
}
