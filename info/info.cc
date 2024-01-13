#include "info/info.h"
#include <iostream>
#include <vector>
namespace leveldb {
std::map<int, FileInfo> kGlobalInfo;

void PrintInfo() {
  uint64_t level_info[10][2];
  memset(level_info, 0, sizeof(level_info));
  for (auto [number, info] : kGlobalInfo) {
    // std::cout << "文件号：" << number << " 层级：" << info.level << " 寿命" << info.live_time << " ms" << std::endl;
    if (!info.is_live) {
      level_info[info.level][0]++;
      level_info[info.level][1] += info.live_time;
    }
  }
  for (int i = 0; i < 10; i++) {
    if (level_info[i][0] != 0) {
      printf("level %d:num:%d avg live time:%dms \n", i, level_info[i][0], level_info[i][1] / level_info[i][0]);
    }
  }
}
}  // namespace leveldb
