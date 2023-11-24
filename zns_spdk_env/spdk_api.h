#include "spdk/stdinc.h"
#include "spdk/thread.h"
#include "spdk/bdev.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/string.h"
#include "spdk/bdev_zone.h"
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace leveldb {

class Semaphore {  // 使用互斥锁与条件变量实现信号量
 public:
  explicit Semaphore(int count = 0) : count_(count) {}

  void Signal() {
    std::unique_lock<std::mutex> lock(mutex_);
    ++count_;
    cv_.notify_one();
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [=] { return count_ > 0; });
    --count_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int count_;
};

struct SpdkContext {  // 回调函数参数类型
  Semaphore sem;
  std::atomic<int> unfinish_op;
  uint64_t lba;
};

class SpdkApi {  // 简单封装的SPDK API
 public:
  static void AppStart(SpdkContext* context);
  static void AppStop(int rc);
  static int AppRead(char* data, uint64_t lba, int num_block,
                     SpdkContext* context);
  static int AppWrite(char* data, uint64_t slba, int num_block,
                      SpdkContext* context);
};
}  // namespace leveldb