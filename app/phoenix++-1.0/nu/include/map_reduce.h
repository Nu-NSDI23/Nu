/* Copyright (c) 2007-2011, Stanford University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Stanford University nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY STANFORD UNIVERSITY ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL STANFORD UNIVERSITY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MAP_REDUCE_H_
#define MAP_REDUCE_H_

extern "C" {
#include <runtime/runtime.h>
#include <runtime/timer.h>
}
#include <algorithm>
#include <cereal/types/vector.hpp>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <memory>
#include <nu/commons.hpp>
#include <nu/dis_hash_table.hpp>
#include <nu/runtime.hpp>
#include <nu/utils/robin_hood.h>
#include <queue>
#include <span>
#include <sync.h>
#include <thread.h>
#include <unordered_map>
#include <vector>

#include "combiner.h"
#include "stddefines.h"

uint64_t map_start_us, shuffle_start_ts, shuffle_end_ts;

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner =
              buffer_combiner,
          class Hash = robin_hood::hash<K>>
class MapReduce {
public:
  /* Standard data types for the function arguments and results */
  typedef D data_type;
  typedef V value_type;
  typedef K key_type;
  typedef std::unordered_map<K, Combiner<V, std::allocator>, Hash>
      map_container;
  typedef Combiner<V, std::allocator>::combined reduce_iterator;

  struct keyval {
    key_type key;
    value_type val;

    template <class Archive> void serialize(Archive &ar) { ar(key, val); }
  };

  constexpr static uint32_t kDefaultChunkSize = 1;
  constexpr static uint32_t kDefaultNumBucketsPerHashTableShard = 64;

  using HashTable = nu::DistributedHashTable<
      K, typename Combiner<V, std::allocator>::combined, Hash, std::equal_to<K>,
      kDefaultNumBucketsPerHashTableShard>;

protected:
  std::vector<std::vector<keyval>> final_vals; // Array to send to merge task.

  // the default split function...
  int split(data_type &a) { return 0; }

  // the default map function...
  void map(data_type &a, map_container &m) const {}

  // the default reduce function...
  void reduce(key_type const &key, reduce_iterator const &values,
              std::vector<keyval> &out) const {
    value_type val;
    while (values.next(val)) {
      keyval kv = {key, val};
      out.push_back(kv);
    }
  }

public:
  MapReduce() {}

  MapReduce(uint64_t num_worker_threads) {
    auto ht = nu::make_dis_hash_table<
        K, typename Combiner<V, std::allocator>::combined, Hash,
        std::equal_to<K>, kDefaultNumBucketsPerHashTableShard>(
        nu::bsr_64(num_worker_threads - 1) + 1);
    for (uint64_t i = 0; i < num_worker_threads; i++) {
      worker_threads.emplace_back(nu::make_proclet<Impl>());
      worker_threads.back().run(
          +[](Impl &impl, HashTable hash_table) {
            impl.init(std::move(hash_table));
          },
          ht);
    }
    hash_table.reset(new HashTable(std::move(ht)));
  }

  void init(HashTable ht) {
    hash_table.reset(new HashTable(std::move(ht)));
    map_container_ptr.reset(new map_container());
  }

  template <typename... S0s, typename... S1s>
  void for_all_worker_threads(void (*fn)(Impl &, S0s...), S1s &&... states);

  /* The main MapReduce engine. This is the function called by the
   * application. It is responsible for creating and scheduling all map
   * and reduce tasks, and also organizes and maintains the data which is
   * passed from application to map tasks, map tasks to reduce tasks, and
   * reduce tasks back to the application. Results are stored in result.
   * A return value less than zero represents an error. This function is
   * not thread safe.
   */
  int run(data_type *data, uint64_t count, std::vector<keyval> &result,
          uint64_t chunk_size = kDefaultChunkSize);

  // This version assumes that the split function is provided.
  int run(std::vector<keyval> &result, uint64_t chunk_size = kDefaultChunkSize);

  virtual void run_map(data_type *data, uint64_t len, uint64_t chunk_size);

  virtual void run_reduce();

  virtual void run_merge();

  void emit_intermediate(map_container &i, key_type const &k,
                         value_type const &v) const {
    i[k].add(v);
  }

private:
  std::unique_ptr<HashTable> hash_table;
  std::vector<nu::Proclet<Impl>> worker_threads;
  std::unique_ptr<map_container> map_container_ptr;

  void map_chunk(std::vector<data_type> data_chunk);
  void shuffle();
  void __run_map(data_type *data, uint64_t len, uint64_t chunk_size);
  void __run_map_bsp(data_type *data, uint64_t len, uint64_t chunk_size);
};

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
int MapReduce<Impl, D, K, V, Combiner, Hash>::run(std::vector<keyval> &result,
                                                  uint64_t chunk_size) {
  timespec begin;
  std::vector<D> data;
  uint64_t count;
  D chunk;

  // Run splitter to generate chunks
  get_time(begin);
  while (static_cast<Impl *>(this)->split(chunk)) {
    data.push_back(chunk);
  }
  count = data.size();
  print_time_elapsed("split phase", begin);

  return run(&data[0], count, result, chunk_size);
}

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
int MapReduce<Impl, D, K, V, Combiner, Hash>::run(D *data, uint64_t count,
                                                  std::vector<keyval> &result,
                                                  uint64_t chunk_size) {
  map_start_us = microtime();
  run_map(data, count, chunk_size);
  shuffle_start_ts = microtime();
  run_reduce();
  run_merge();
  result.swap(this->final_vals[0]);

  std::cout << shuffle_start_ts - map_start_us << " "
            << shuffle_end_ts - shuffle_start_ts << " " << std::endl;

  return 0;
}

/**
 * Run map tasks and get intermediate values
 */
template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
void MapReduce<Impl, D, K, V, Combiner, Hash>::run_map(data_type *data,
                                                       uint64_t count,
                                                       uint64_t chunk_size) {
#ifndef BSP
  __run_map(data, count, chunk_size);
#else
  __run_map_bsp(data, count, chunk_size);
#endif
}

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
void MapReduce<Impl, D, K, V, Combiner, Hash>::__run_map(data_type *data,
                                                         uint64_t count,
                                                         uint64_t chunk_size) {
  std::vector<nu::Future<void>> futures;
  futures.resize(worker_threads.size());

  uint32_t dispatch_id = 0;
  std::vector<data_type> data_chunk;

  for (uint64_t start_id = 0; start_id < count; start_id += chunk_size) {
    auto chunk_idx_begin = start_id;
    auto chunk_idx_end = std::min(chunk_size + start_id, count);

    data_chunk.clear();
    for (uint32_t i = chunk_idx_begin; i < chunk_idx_end; i++) {
      data_chunk.push_back(data[i]);
    }

    while (futures[dispatch_id] && !futures[dispatch_id].is_ready()) {
      dispatch_id++;
      if (unlikely(dispatch_id == worker_threads.size())) {
        dispatch_id = 0;
      }
    }

    auto *general_worker_thread = reinterpret_cast<nu::Proclet<MapReduce> *>(
        &worker_threads[dispatch_id]);
    futures[dispatch_id] =
        general_worker_thread->run_async(&MapReduce::map_chunk, data_chunk);
  }
}

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
void MapReduce<Impl, D, K, V, Combiner, Hash>::__run_map_bsp(
    data_type *data, uint64_t count, uint64_t chunk_size) {
  std::vector<nu::Future<void>> futures;
  futures.resize(worker_threads.size());

#ifdef BSP_PRINT_STAT
  std::ofstream bsp_stat_ofs("bsp_stat");
  std::vector<uint64_t> timestamps;
  timestamps.reserve((count - 1) / worker_threads.size() + 1);
#endif

  for (uint64_t start_id = 0; start_id < count;
       start_id += worker_threads.size()) {
#ifdef BSP_PRINT_STAT
    timestamps.push_back(microtime());
#endif
    auto batch_size = std::min(worker_threads.size(), count - start_id);
    for (uint32_t i = 0; i < batch_size; i++) {
      std::vector<data_type> data_chunk(1, data[start_id + i]);
      futures[i] =
          worker_threads[i].run_async(&MapReduce::map_chunk, data_chunk);
    }

    for (auto &future : futures) {
      future.get();
    }
  }

#ifdef BSP_PRINT_STAT
  for (auto timestamp : timestamps) {
    bsp_stat_ofs << timestamp << std::endl;
  }
#endif
}

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
void MapReduce<Impl, D, K, V, Combiner, Hash>::shuffle() {
  constexpr uint32_t kMaxConcurrency = 8;

  std::queue<nu::Future<void>> futures;
  auto iter = map_container_ptr->begin();
  while (iter != map_container_ptr->end()) {
    auto &[k, combiner] = *iter;
    futures.emplace(hash_table->apply_async(
        std::move(k),
        +[](std::pair<const K, typename Combiner<V, std::allocator>::combined>
                &p,
            Combiner<V, std::allocator> &&combiner) {
          combiner.combineinto(p.second);
        },
        std::move(combiner)));
    if (futures.size() > kMaxConcurrency) {
      futures.pop();
    }
    iter = map_container_ptr->erase(iter);
  }
}

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
void MapReduce<Impl, D, K, V, Combiner, Hash>::map_chunk(
    std::vector<data_type> data_chunk) {
  for (auto &data : data_chunk) {
    static_cast<Impl const *>(this)->map(data, *map_container_ptr);
  }
}

/**
 * Run reduce tasks and get final values.
 */
template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
void MapReduce<Impl, D, K, V, Combiner, Hash>::run_reduce() {
  std::vector<nu::Future<void>> futures;
  futures.reserve(worker_threads.size());
  for (auto &worker_thread : worker_threads) {
    auto *general_worker_thread =
        reinterpret_cast<nu::Proclet<MapReduce> *>(&worker_thread);
    futures.emplace_back(general_worker_thread->run_async(&MapReduce::shuffle));
  }
  for (auto &future : futures) {
    future.get();
  }
  shuffle_end_ts = microtime();

  final_vals = hash_table->associative_reduce(
      /* clear = */ true, /* init_val = */ std::vector<keyval>(),
      /* reduce_fn =  */
      +[](std::vector<keyval> &pairs,
          std::pair<const K, typename Combiner<V, std::allocator>::combined>
              &p) {
        Impl *stateless = nullptr;
        stateless->reduce(p.first, p.second, pairs);
      });
}

/**
 * Merge all reduced data
 */
template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
void MapReduce<Impl, D, K, V, Combiner, Hash>::run_merge() {
  uint32_t indices[final_vals.size() + 1];
  indices[0] = 0;

  for (uint32_t i = 0; i < final_vals.size(); i++) {
    auto &partition = final_vals[i];
    indices[i + 1] = indices[i] + partition.size();
  }
  std::vector<std::vector<keyval>> final(1);
  final[0].resize(indices[final_vals.size()]);

  std::vector<rt::Thread> threads;
  for (uint32_t i = 0; i < final_vals.size(); i++) {
    threads.emplace_back([&, tid = i] {
      auto &partition = final_vals[tid];
      for (uint32_t j = indices[tid]; j < indices[tid + 1]; j++) {
        final[0][j] = partition[j - indices[tid]];
      }
    });
  }
  for (auto &thread : threads) {
    thread.Join();
  }

  this->final_vals = std::move(final);
}

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner,
          class Hash>
template <typename... S0s, typename... S1s>
void MapReduce<Impl, D, K, V, Combiner, Hash>::for_all_worker_threads(
    void (*fn)(Impl &, S0s...), S1s &&... states) {
  nu::MigrationGuard g;
  std::unordered_map<uint32_t, std::vector<nu::WeakProclet<Impl>>>
      ip_to_proclets;

  auto *rpc_client_mgr = nu::get_runtime()->rpc_client_mgr();
  for (auto &worker_thread : worker_threads) {
    auto ip = rpc_client_mgr->get_ip_by_proclet_id(worker_thread.get_id());
    ip_to_proclets[ip].push_back(worker_thread.get_weak());
  }

  auto fn_addr = reinterpret_cast<uintptr_t>(fn);
  std::vector<nu::Future<std::vector<uint32_t>>> fn_futures;

  for (auto &[_, proclets] : ip_to_proclets) {
    BUG_ON(proclets.empty());
    fn_futures.emplace_back(nu::async([&, proclets]() mutable {
      auto &worker_thread = proclets.front();
      return worker_thread.run(
          +[](Impl &_, std::vector<nu::WeakProclet<Impl>> proclets,
              uintptr_t fn_addr, S0s... states) {
            auto *fn = reinterpret_cast<void (*)(Impl &, S0s...)>(fn_addr);
            std::vector<nu::Future<uint32_t>> futures;
            std::vector<uint32_t> ips;

            for (auto worker_thread : proclets) {
              futures.emplace_back(nu::async([&, worker_thread]() mutable {
                worker_thread.run(fn, states...);
		nu::MigrationGuard g;
                return nu::get_runtime()
                    ->rpc_client_mgr()
                    ->get_ip_by_proclet_id(worker_thread.get_id());
              }));
            }

            for (auto &future : futures) {
              ips.push_back(future.get());
            }
            return ips;
          },
          proclets, fn_addr, std::forward<S1s>(states)...);
    }));
  }

  std::vector<nu::Future<void>> update_loc_futures;
  for (auto fn_future_iter = fn_futures.begin(),
            proclet_iter = ip_to_proclets.begin();
       fn_future_iter < fn_futures.end(); fn_future_iter++, proclet_iter++) {
    auto &ips = fn_future_iter->get();
    auto &proclets = proclet_iter->second;
    for (size_t i = 0; i < ips.size(); i++) {
      auto ip = ips[i];
      auto &worker_thread = proclets[i];
      if (unlikely(ip != rpc_client_mgr->get_ip_by_proclet_id(
                             worker_thread.get_id()))) {
        update_loc_futures.emplace_back(
            worker_thread.run_async(+[](Impl &_) {}));
      }
    }
  }
}

template <typename Impl, typename D, typename K, typename V,
          template <typename, template <class> class> class Combiner =
              buffer_combiner,
          class Hash = std::hash<K>>
class MapReduceSort : public MapReduce<Impl, D, K, V, Combiner, Hash> {
public:
  typedef typename MapReduce<Impl, D, K, V, Combiner, Hash>::keyval keyval;

protected:
  // default sorting order is by key. User can override.
  bool sort(keyval const &a, keyval const &b) const { return a.key < b.key; }

  struct sort_functor {
    MapReduceSort *mrs;
    sort_functor(MapReduceSort *mrs) : mrs(mrs) {}
    bool operator()(keyval const &a, keyval const &b) const {
      return static_cast<Impl const *>(mrs)->sort(a, b);
    }
  };

  virtual void run_merge() {
    // how many lists to merge in a single task.
    static const int merge_factor = 2;
    int merge_queues = this->final_vals.size();

    std::vector<rt::Thread> threads;
    // First sort each queue in place
    for (int i = 0; i < merge_queues; i++) {
      threads.emplace_back(
          [&, i, val = &this->final_vals[i]] { merge_fn(val, 0, i); });
    }

    for (auto &thread : threads) {
      thread.Join();
    }
    threads.clear();

    // Then merge
    std::vector<std::vector<keyval>> merge_vals;
    while (merge_queues > 1) {
      uint64_t resulting_queues =
          (uint64_t)std::ceil(merge_queues / (double)merge_factor);

      // swap queues
      merge_vals = std::move(this->final_vals);
      this->final_vals.clear();
      this->final_vals.resize(resulting_queues);

      // distribute tasks into task queues using locality information
      // if provided.
      int queue_index = 0;
      for (uint64_t i = 0; i < resulting_queues; i++) {
        int actual = std::min(merge_factor, merge_queues - queue_index);
        threads.emplace_back([&, val = &merge_vals[queue_index], actual, i] {
          merge_fn(val, actual, i);
        });
        queue_index += actual;
      }

      for (auto &thread : threads) {
        thread.Join();
      }
      threads.clear();

      merge_vals.clear();
      merge_queues = resulting_queues;
    }

    assert(merge_queues == 1);
  }

  void merge_fn(std::vector<keyval> *vals, uint64_t length,
                uint64_t out_index) {
    if (length == 0) {
      // this case really just means sort my list in place.
      // stable_sort ensures that the order of same keyvals with
      // the same key emitted in reduce remains the same in sort
      std::stable_sort(vals->begin(), vals->end(), sort_functor(this));
    } else if (length == 1) {
      // if only one list, don't merge, just move over.
      (*vals).swap(this->final_vals[out_index]);
    } else if (length == 2) {
      // stl merge is nice and fast for 2.
      this->final_vals[out_index].resize(vals[0].size() + vals[1].size());
      std::merge(vals[0].begin(), vals[0].end(), vals[1].begin(), vals[1].end(),
                 this->final_vals[out_index].begin(), sort_functor(this));
    } else {
      // for more, do a multiway merge.
      assert(0);
    }
  }
};

#endif // MAP_REDUCE_H_

// vim: ts=8 sw=4 sts=4 smarttab smartindent
