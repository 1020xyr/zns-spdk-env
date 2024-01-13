#pragma once
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>

namespace leveldb {
const int kMemSize = 1 << 27;
const int kBlockSize = 4096;
const int kZoneSize = kMemSize / kBlockSize;
const int kZoneNum = 10;

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
struct AppAppendJobArg {
  char* data;
  uint64_t slba;
  int num_block;
  SpdkAppendContext context;
};

struct Zone {
  char* wp;
  char data[kMemSize];
};

class FackZNSDevice {
 public:
  static FackZNSDevice* GetInstance() {
    static FackZNSDevice device;
    return &device;
  }
  ~FackZNSDevice() = default;

  int AppRead(AppReadJobArg* arg);
  int AppAppend(AppAppendJobArg* arg);
  uint64_t GetWritePointLba(int zone_id) { return zone_id * kZoneSize + (zones_[zone_id].wp - zones_[zone_id].data) / kBlockSize; }

 private:
  FackZNSDevice();

  Zone zones_[kZoneNum];
};

};  // namespace leveldb