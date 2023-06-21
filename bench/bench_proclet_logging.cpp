#include <iostream>
#include <vector>
#include <memory>
#include <cassert>

#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/dis_hash_table.hpp"

using namespace nu;
using namespace std;

constexpr static uint32_t kNumThreads = 1840;
constexpr static uint32_t kObjSize = 100;
constexpr static uint32_t kBufSize = 102400;
constexpr static uint32_t kNumInvocationsPerThread = 1000;

struct Obj {
  uint8_t data[kObjSize];
};

using Buf = std::vector<Obj>;


class TableDB {
  public:
    TableDB()
    : _map(nu::make_dis_hash_table<int64_t, int64_t, std::hash<int64_t>>(9)){}

    void put(int64_t key, int64_t value){
      _map.put(key, value);
    }
    int64_t get(int64_t key){
      return _map.get(key).value();
    }
  private:
    nu::DistributedHashTable<int64_t, int64_t, std::hash<int64_t>> _map;
};


class Worker {
 public:
  Worker(Proclet<TableDB> tabledb)
  : _tabledb(std::move(tabledb)){}

  bool foo(uint32_t kNumThreads) {
    for (int64_t i = 0; i < kNumThreads; i++){
      int64_t v = _tabledb.run(&TableDB::get, i);
      if (v != i){
        std::cout << "BADRESULT" << v;
      }
    }
    return true;
  }
  private:
    Proclet<TableDB> _tabledb;
};



void do_work() {
  std::vector<Proclet<Worker>> workers;
  auto dis_hash = make_proclet<TableDB>();

  for (int64_t i = 0; i < kNumThreads; i++) {
    dis_hash.run(&TableDB::put, i, i);
  }
  for (int64_t i = 0; i < kNumThreads; i++) {
    workers.emplace_back(make_proclet<Worker>(
      std::forward_as_tuple(dis_hash)
    ));
  }

  std::vector<rt::Thread> ths;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    ths.emplace_back([worker = std::move(workers[i])]() mutable {
      //Buf buf;
      //buf.resize(kBufSize / kObjSize);
      for (uint32_t j = 0; j < kNumInvocationsPerThread; j++) {
        bool result = worker.run(&Worker::foo, kNumThreads);
        BUG_ON(!result);
        //if (!result){
          //std::cout << "Wrong output from foo" << std::endl;
        //}
      }
      //std::cout << "Finished execution for worker " << i << std::endl;
    });
  }

  auto t0 = microtime();
  for (auto &th : ths) {
    th.Join();
  }
  auto t1 = microtime();
  auto us = t1 - t0;
  //auto size =
  //    static_cast<uint64_t>(kBufSize) * kNumInvocationsPerThread * kNumThreads;
  auto size = sizeof(int64_t) * kNumThreads * kNumThreads + (kNumThreads * (1 + sizeof(int64_t)));
  auto mbs = size / us;



  std::cout << t1 - t0 << " us, " << mbs << " MB/s" << std::endl;
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
