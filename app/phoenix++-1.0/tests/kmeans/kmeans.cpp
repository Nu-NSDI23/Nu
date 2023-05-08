/* Copyright (c) 2007-2011, Stanford University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *      * Neither the name of Stanford University nor the names of its
 *         contributors may be used to endorse or promote products derived from
 *         this software without specific prior written permission.
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

#include <algorithm>
#include <array>
#include <cereal/archives/binary.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>

#include "map_reduce.h"

constexpr int kNumPoints = 10000; // Number of vectors
constexpr int kDim = 3;           // Dimension of each vector
constexpr int kNumMeans = 100;    // Number of clusters
constexpr int kGridSize = 1000;   // Size of each dimension of vector space
constexpr bool kDumpResult = false;

constexpr int kNumWorkerNodes = 1;
constexpr int kNumWorkerThreads = 1;

struct point {
  int d[kDim];
  int cluster; // cluster point count (for means)

  template <class Archive> void serialize(Archive &ar) { ar(d, cluster); }

  point() { cluster = -1; }

  point(int cluster) { this->cluster = cluster; }

  point(int *d, int cluster) {
    memcpy(this->d, d, sizeof(this->d));
    this->cluster = cluster;
  }

  point &normalize() {
    for (int i = 0; i < kDim; ++i)
      d[i] /= cluster;
    cluster = 1;
    return *this;
  }

  unsigned int sq_dist(point const &p) {
    unsigned int sum = 0;
    for (int i = 0; i < kDim; i++) {
      int diff = d[i] - p.d[i];
      sum += diff * diff;
    }
    return sum;
  }

  void dump() {
    for (int j = 0; j < kDim; j++)
      printf("%5d ", d[j]);
    printf("\n");
  }

  void generate() {
    for (int j = 0; j < kDim; j++)
      d[j] = rand() % kGridSize;
  }

  bool operator==(const point &o) const {
    return std::equal(std::begin(this->d), std::end(this->d), o.d);
  }
};

std::array<point, kNumPoints> points;

template <class V, template <class> class Allocator>
class point_combiner
    : public associative_combiner<point_combiner<V, Allocator>, V, Allocator> {
public:
  static void F(point &a, point const &b) {
    a.cluster += b.cluster;
    for (int i = 0; i < kDim; i++)
      a.d[i] += b.d[i];
  }
  static void Init(point &a) {
    std::fill(std::begin(a.d), std::end(a.d), 0);
    a.cluster = 0;
  }
  static bool Empty(point const &a) { return a.cluster == 0; }
};

using task_id = int;

class KmeansMR
    : public MapReduce<KmeansMR, task_id, intptr_t, point, point_combiner> {
public:
  std::vector<point> means;

  KmeansMR() {}

  KmeansMR(uint64_t num_worker_nodes, uint64_t num_worker_threads)
      : MapReduce(num_worker_nodes, num_worker_threads) {}

  void map(data_type &task_id, map_container &out) const {
    unsigned int min_dist = std::numeric_limits<unsigned int>::max();
    uint64_t min_idx = 0;
    auto &p = points[task_id];

    for (size_t j = 0; j < means.size(); j++) {
      unsigned int cur_dist = p.sq_dist(means[j]);
      if (cur_dist < min_dist) {
        min_dist = cur_dist;
        min_idx = j;
      }
    }

    emit_intermediate(out, min_idx, point(p.d, 1));
  }
};

void real_main(int argc, char **argv) {
  srand(0);

  std::vector<point> generated_points(kNumPoints);
  for (int i = 0; i < kNumPoints; i++) {
    generated_points[i].generate();
  }

  std::vector<point> means;
  for (int i = 0; i < kNumMeans; i++) {
    means.emplace_back(0);
    means[i].generate();
  }

  printf("KMeans: Calling MapReduce Scheduler\n");

  KmeansMR mapReduce(kNumWorkerNodes, kNumWorkerThreads);

  mapReduce.for_all_worker_nodes(
      +[](std::vector<point> generated_points) {
        std::copy(generated_points.begin(), generated_points.end(),
                  points.begin());
      },
      generated_points);

  std::vector<task_id> tasks;
  for (int i = 0; i < kNumPoints; i++) {
    tasks.push_back(i);
  }

  bool modified;
  int iter = 0;
  do {
    std::cout << "iter = " << iter++ << std::endl;
    mapReduce.for_all_worker_threads(
        +[](KmeansMR &mr, std::vector<point> means) {
          mr.means = std::move(means);
        },
        means);

    std::vector<KmeansMR::keyval> result;
    BUG_ON(mapReduce.run(tasks.data(), kNumPoints, result) < 0);

    modified = false;
    for (size_t i = 0; i < result.size(); i++) {
      auto new_mean = result[i].val.normalize();
      auto &mean = means[result[i].key];
      if (mean != new_mean) {
        modified = true;
        mean = new_mean;
      }
    }
  } while (modified);

  printf("KMeans: MapReduce Completed\n");

  if constexpr (kDumpResult) {
    printf("\n\nFinal means:\n");
    for (int i = 0; i < kNumMeans; i++)
      means[i].dump();
  }
}

int main(int argc, char **argv) {
  nu::runtime_main_init(argc, argv,
                        [](int argc, char **argv) { real_main(argc, argv); });
}

// vim: ts=8 sw=4 sts=4 smarttab smartindent
