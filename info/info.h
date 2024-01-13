#pragma once
#include <map>
#include <string>
#include <chrono>
#include "db/filename.h"

namespace leveldb {
struct FileInfo {
  FileType type;
  int level;
  std::chrono::_V2::system_clock::time_point create_time;
  std::chrono::_V2::system_clock::time_point delete_time;
  uint64_t live_time;
  uint64_t file_size;
  std::string smallest_key;
  std::string largest_key;
  bool is_live;
};

extern std::map<int, FileInfo> kGlobalInfo;

void PrintInfo();

}  // namespace leveldb