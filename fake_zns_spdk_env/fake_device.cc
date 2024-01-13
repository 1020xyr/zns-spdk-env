#include "fake_zns_spdk_env/fake_device.h"
#include <cassert>
namespace leveldb {

FackZNSDevice::FackZNSDevice() {
  for (int i = 0; i < kZoneNum; i++) {  // 初始化各个分区
    memset(zones_[i].data, 0x0, kMemSize);
    zones_[i].wp = zones_[i].data;
  }
}

int FackZNSDevice::AppRead(AppReadJobArg* arg) {
  // 计算对应分区号与分区偏移
  int zone_idx = arg->lba / kZoneSize;
  int zone_offset = arg->lba % kZoneSize;
  int num_block = arg->num_block;
  // 将分区数据读取至缓冲区
  memcpy(arg->data, (void*)(zones_[zone_idx].data + zone_offset * kBlockSize), num_block * kBlockSize);

  // 信号量相关操作
  arg->context->unfinish_op.fetch_sub(1);
  if (arg->context->unfinish_op.load() == 0) {  // 所有读操作均已完成
    arg->context->sem.Signal();
  }
}

int FackZNSDevice::AppAppend(AppAppendJobArg* arg) {
  static std::mutex append_lock;
  append_lock.lock();
  int zone_idx = arg->slba / kZoneSize;
  assert(zone_idx < kZoneNum);
  // 将缓冲区数据写入对应分区
  memcpy(zones_[zone_idx].wp, arg->data, arg->num_block * kBlockSize);
  // 记录成功写入的地址
  *arg->context.lba = GetWritePointLba(zone_idx);
  // printf("append lba:%lld\n", *arg->context.lba);
  zones_[zone_idx].wp += arg->num_block * kBlockSize;

  // 信号量相关操作
  arg->context.share->unfinish_op.fetch_sub(1);
  if (arg->context.share->unfinish_op.load() == 0 && arg->context.share->closed == true) {  // 所有的写操作已完成
    arg->context.share->sem.Signal();
  }
  append_lock.unlock();
}

};  // namespace leveldb