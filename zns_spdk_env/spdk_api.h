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

  void WaitFor(std::chrono::seconds second) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, second);
    --count_;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int count_;
};

struct SpdkInfo {  // SPDK的一些相关信息
  struct spdk_bdev* bdev;
  struct spdk_bdev_desc* bdev_desc;
  struct spdk_io_channel* bdev_io_channel;
  spdk_thread* app_thread;
};

extern SpdkInfo g_spdk_info;

struct SpdkContext {  // 回调函数参数类型
  Semaphore sem;
  std::atomic<int> unfinish_op;
  bool closed;
};

struct SpdkAppendContext {  // Append回调函数参数类型
  SpdkContext* share;
  uint64_t* lba;
};
// 读操作参数
struct AppReadJobArg {
  char* data;
  uint64_t lba;
  int num_block;
  SpdkContext* context;
};

// 写操作参数
struct AppWriteJobArg {
  char* data;
  uint64_t slba;
  int num_block;
  SpdkAppendContext context;
};

void AppStart(SpdkContext* context);  // 启动spdk app
void AppStop();                       // 停止spdk app
int AppRead(AppReadJobArg* arg);    
int AppWrite(AppWriteJobArg* arg);

}  // namespace leveldb