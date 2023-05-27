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
 *     * Neither the name of Stanford University nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
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

#include <cereal/archives/binary.hpp>
#include <fcntl.h>
#include <nu/runtime.hpp>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread.h>

#include "map_reduce.h"

constexpr char fname_A[] = "matrix_file_A.txt";
constexpr char fname_B[] = "matrix_file_B.txt";
constexpr uint32_t kNumWorkerThreads = 46 * 14;
constexpr uint32_t kNumWorkerNodes = 14;

struct mm_data_t {
  uint64_t matrix_len;
  uint64_t row_id;

  template <class Archive> void serialize(Archive &ar) {
    ar(matrix_len, row_id);
  }
};

using Row = std::vector<int>;

int fd_A, fd_B, file_size;
int *matrix_A, *matrix_B;

class WorkerNodeInitializer {
public:
  WorkerNodeInitializer() {
    struct stat finfo_A;
    CHECK_ERROR((fd_A = open(fname_A, O_RDONLY)) < 0);
    CHECK_ERROR((fd_B = open(fname_B, O_RDONLY)) < 0);
    CHECK_ERROR(fstat(fd_A, &finfo_A) < 0);
    file_size = finfo_A.st_size;
    int flags = MAP_PRIVATE | MAP_POPULATE | MAP_FIXED_NOREPLACE;
    CHECK_ERROR((matrix_A = (int *)mmap(
                     reinterpret_cast<void *>(0x600000000000), file_size + 1,
                     PROT_READ, flags, fd_A, 0)) == (void *)-1);
    CHECK_ERROR((matrix_B = (int *)mmap(
                     reinterpret_cast<void *>(0x610000000000), file_size + 1,
                     PROT_READ, flags, fd_B, 0)) == (void *)-1);
  }  
};

class MatrixMulMR : public MapReduce<MatrixMulMR, mm_data_t, uint64_t, Row> { 
public:  
  int matrix_size;
  int row;

  explicit MatrixMulMR(int size)
      : MapReduce<MatrixMulMR, mm_data_t, uint64_t, Row>(kNumWorkerThreads),
        matrix_size(size), row(0) {}

  /** matrixmul_map()
   * Multiplies the allocated regions of matrix to compute partial sums
   */
  void map(mm_data_t const &data, map_container &out) const {
    auto *tmp_A = matrix_A;
    auto *tmp_B = matrix_B;
    tmp_A += data.matrix_len * data.row_id;

    std::vector<int> output(data.matrix_len);
    for (size_t i = 0; i < data.matrix_len; i++) {
      for (size_t j = 0; j < data.matrix_len; j++) {
        output[j] += tmp_A[i] * tmp_B[j];
      }
      tmp_B += data.matrix_len;
    }
    emit_intermediate(out, data.row_id, output);
  }

  /** matrixmul_split()
   *  Assign a set of rows of the output matrix to each map task
   * (for now, default to one row per task)
   */
  int split(mm_data_t &out) {
    /* End of data reached, return FALSE. */
    if (row >= matrix_size) {
      return 0;
    }

    out.matrix_len = matrix_size;
    out.row_id = row++;

    /* Return true since the out data is valid. */
    return 1;
  }
};

void real_main(int argc, char *argv[]) {

  int i, j, create_files;
  int fd_A, fd_B, file_size;
  int matrix_len, row_block_len;
  int ret;

  struct timespec begin, end;

  get_time(begin);

  srand((unsigned)time(NULL));

  // Make sure a filename is specified
  if (argc < 2) {
    dprintf("USAGE: %s [side of matrix] [size of Row block]\n", argv[0]);
    exit(1);
  }

  CHECK_ERROR((matrix_len = atoi(argv[1])) < 0);
  file_size = ((matrix_len * matrix_len)) * sizeof(int);

  fprintf(stderr, "***** file size is %d\n", file_size);

  if (argc < 3)
    row_block_len = 1;
  else
    CHECK_ERROR((row_block_len = atoi(argv[2])) < 0);

  if (argc >= 4)
    create_files = 1;
  else
    create_files = 0;

  printf("MatrixMult: Side of the matrix is %d\n", matrix_len);
  printf("MatrixMult: Row Block Len is %d\n", row_block_len);
  printf("MatrixMult: Running...\n");

  /* If the matrix files do not exist, create them */
  if (create_files) {
    dprintf("Creating files\n");

    int value = 0;
    CHECK_ERROR((fd_A = open(fname_A, O_CREAT | O_RDWR, S_IRWXU)) < 0);
    CHECK_ERROR((fd_B = open(fname_B, O_CREAT | O_RDWR, S_IRWXU)) < 0);

    for (i = 0; i < matrix_len; i++) {
      for (j = 0; j < matrix_len; j++) {
        value = (rand()) % 11;
        ret = write(fd_A, &value, sizeof(int));
        assert(ret == sizeof(int));
        // dprintf("%d  ",value);
      }
      // dprintf("\n");
    }
    // dprintf("\n");

    for (i = 0; i < matrix_len; i++) {
      for (j = 0; j < matrix_len; j++) {
        value = (rand()) % 11;
        ret = write(fd_B, &value, sizeof(int));
        assert(ret == sizeof(int));
        // dprintf("%d  ",value);
      }
      // dprintf("\n");
    }

    CHECK_ERROR(close(fd_A) < 0);
    CHECK_ERROR(close(fd_B) < 0);
  }

  printf("MatrixMult: Calling MapReduce Scheduler Matrix Multiplication\n");

  get_time(end);
  print_time("initialize", begin, end);

  get_time(begin);
  std::vector<MatrixMulMR::keyval> result;
  MatrixMulMR mapReduce(matrix_len);

  nu::RemObj<WorkerNodeInitializer> rem_initializers[kNumWorkerNodes];
  for (uint32_t i = 0; i < kNumWorkerNodes; i++) {
    rem_initializers[i] = nu::RemObj<WorkerNodeInitializer>::create();
  }

  mapReduce.run(result);
  get_time(end);
  print_time("library", begin, end);

  get_time(begin);
  int sum = 0;
  for (auto [_, row] : result) {
    for (auto num : row) {
      sum += num;
    }
  }
  printf("MatrixMult: total sum is %d\n", sum);

  printf("MatrixMult: MapReduce Completed\n");
  get_time(end);
  print_time("finalize", begin, end);
}

int main(int argc, char **argv) {
  nu::runtime_main_init(argc, argv,
                        [](int argc, char **argv) { real_main(argc, argv); });
}

// vim: ts=8 sw=4 sts=4 smarttab smartindent
