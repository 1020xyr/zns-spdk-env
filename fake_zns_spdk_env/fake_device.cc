#include "fake_zns_spdk_env/fake_device.h"
#include <cassert>
namespace leveldb {

FackZNSDevice::FackZNSDevice() {
  for (int i = 0; i < kZoneNum; i++) {
    memset(zones_[i].data, 0x0, kMemSize);
    zones_[i].wp = zones_[i].data;
  }
}
int FackZNSDevice::AppRead(AppReadJobArg* arg) {
  uint64_t lba = arg->lba;
  int num_block = arg->num_block;
  int offset = 0;
  while (num_block > 0) {
    int zone_idx = lba / kZoneSize;
    int zone_offset = lba % kZoneSize;
    int num = std::min(num_block, kZoneSize - zone_offset);
    memcpy(arg->data + offset, (void*)(zones_[zone_idx].data + zone_offset * kBlockSize), num * kBlockSize);
    num_block -= num;
    offset += num * kBlockSize;
    lba += num;
  }
  arg->context->unfinish_op.fetch_sub(1);
  if (arg->context->unfinish_op.load() == 0) {  // 所有读操作均已完成
    arg->context->sem.Signal();
  }
}
int FackZNSDevice::AppAppend(AppAppendJobArg* arg) {
  int zone_idx = arg->slba / kZoneSize;
  assert(zone_idx < kZoneNum);
  memcpy(zones_[zone_idx].wp, arg->data, arg->num_block * kBlockSize);
  *arg->context.lba = GetWritePointLba(zone_idx);
  zones_[zone_idx].wp += arg->num_block * kBlockSize;
  arg->context.share->unfinish_op.fetch_sub(1);
  if (arg->context.share->unfinish_op.load() == 0 && arg->context.share->closed == true) {  // 所有的写操作已完成
    arg->context.share->sem.Signal();
  }
}

};  // namespace leveldb