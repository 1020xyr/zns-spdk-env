#include "fake_zns_spdk_env/filesystem.h"
#include <iostream>

namespace leveldb {
// FileStates are reference counted. The initial reference count is zero

const bool kZNSWriteTest = true;
const bool kZNSReadTest = true;
const bool kDataCmpTest = true;

FileState::FileState(std::string fname) : refs_(0), size_(0), seg_num_(0), fname_(fname) {
  static uint64_t total_mem_used = 0;
  total_mem_used += kMaxFileSize;
  // 申请普通内存与大页内存
  mem_buffer_ = new char[kMaxFileSize];
  tmp_buffer_ = new char[kMaxFileSize];

  std::string pure_name = fname.substr(fname.find_last_of('/') + 1);
  ParseFileName(pure_name, &number_, &type_);
  std::cout << fname << " " << pure_name << " " << number_ << " " << type_ << std::endl;
}

// Decrease the reference count. Delete if this is the last reference.
void FileState::Unref() {
  bool do_delete = false;

  {
    std::lock_guard<std::mutex> lk(refs_mutex_);
    --refs_;
    assert(refs_ >= 0);
    if (refs_ <= 0) {
      do_delete = true;
    }
  }

  if (do_delete) {
    delete this;
  }
}

Status FileState::Read(uint64_t offset, size_t n, Slice* result, char* scratch) const {
  std::lock_guard<std::mutex> lk(blocks_mutex_);
  if (offset > size_) {
    return Status::IOError("Offset greater than file size.");
  }
  const uint64_t available = size_ - offset;
  if (n > available) {
    n = static_cast<size_t>(available);
  }
  if (n == 0) {
    *result = Slice();
    return Status::OK();
  }
  std::memcpy(scratch, mem_buffer_ + offset, n);
  *result = Slice(scratch, n);
  return Status::OK();
}

Status FileState::Append(const Slice& data) {
  const char* src = data.data();
  size_t src_len = data.size();

  std::lock_guard<std::mutex> lk(blocks_mutex_);
  std::memcpy(mem_buffer_ + size_, src, src_len);
  size_ += src_len;
  assert(size_ < kMaxFileSize);
  return Status::OK();
}

void FileState::ReadFromZNS() {
  if (type_ != kTableFile) {
    return;
  }
  if (kZNSReadTest == true) {
    SpdkContext context;
    context.unfinish_op = seg_num_;  // 待完成的读操作数
    AppReadJobArg job_args[kMaxSegNum];
    int offset = 0;
    for (int i = 0; i < seg_num_; i++) {
      job_args[i].data = tmp_buffer_ + offset;
      job_args[i].lba = data_seg_start_[i];
      job_args[i].num_block = data_seg_size_[i];
      job_args[i].context = &context;
      // 顺序读取各部分数据
      int rc = FackZNSDevice::GetInstance()->AppRead(&job_args[i]);
      if (rc != 0) {
        printf("read zns data failed.  lba:%lx num:%d\n", data_seg_start_[i], data_seg_size_[i]);
      }
      offset += data_seg_size_[i] * kBlockSize;
      assert(offset < kMaxFileSize);
    }
    if (context.unfinish_op.load() > 0) {  // 等待读取操作全部完成
      context.sem.Wait();
    }
  }

  if (kDataCmpTest == true) {
    static int call_times = 0;
    call_times++;
    int cmp_res = memcmp(tmp_buffer_, mem_buffer_, size_);
    if (cmp_res != 0) {
      int error_block = 0;
      for (int i = 0; i < size_; i++) {
        if (tmp_buffer_[i] != mem_buffer_[i]) {
          printf("start index:%d  block:%d tmp buffer %x mem buffer %x\n", i, i / kBlockSize, tmp_buffer_[i], mem_buffer_[i]);
          error_block = i / kBlockSize;
          break;
        }
      }
      for (int i = size_; i >= 0; i--) {
        if (tmp_buffer_[i] != mem_buffer_[i]) {
          printf("end index:%d  block:%d tmp buffer %x mem buffer %x\n", i, i / kBlockSize, tmp_buffer_[i], mem_buffer_[i]);
          break;
        }
      }
      printf("call times:%d\n", call_times);
      // 读取第一个错误块
      char debug_data[kBlockSize];
      SpdkContext context;
      context.unfinish_op = 1;
      AppReadJobArg debug_arg;
      debug_arg.data = debug_data;
      for (int i = 0; i < seg_num_; i++) {
        if (error_block > data_seg_size_[i]) {
          error_block -= data_seg_size_[i];
        } else {
          debug_arg.lba = data_seg_start_[i] + error_block;
          break;
        }
      }
      debug_arg.num_block = 1;
      debug_arg.context = &context;
      // 顺序读取各部分数据
      int rc = FackZNSDevice::GetInstance()->AppRead(&debug_arg);
      assert(0);
    }
  }
}

uint64_t FileState::PickZone(uint64_t max_lba) {
  const int expect_cap = kZoneSize * 0.8;   // 设置的容量阈值，超过
  const int zone_size = kZoneSize;          // zone size，注意zone size与zone capacity不一致
  if (max_lba % zone_size <= expect_cap) {  // 未超过阈值，选择当前zone即可
    return max_lba / zone_size * zone_size;
  }
  return max_lba / zone_size * zone_size + zone_size;  // 超过阈值，选择下一个zone
}

void FileState::WriteToZNS() {
  if (type_ != kTableFile) {
    return;
  }
  static uint64_t cur_max_lba = 0x0;  // 目前出现的最大lba
  if (kZNSWriteTest == true) {
    // 将数据往对比缓冲区备份一份
    memcpy(tmp_buffer_, mem_buffer_, size_);

    int offset = 0;
    int num_block = (size_ + kBlockSize - 1) / kBlockSize;  // 向上取整
    SpdkContext share;
    share.closed = false;
    share.unfinish_op = 0;
    AppAppendJobArg job_args[kMaxSegNum];
    uint64_t slba = PickZone(cur_max_lba);  // 选择合适的zone写入

    int ret;
    int index = 0;
    while (num_block > 0) {
      int num = std::min(num_block, kMaxTransmit);
      data_seg_size_[seg_num_] = num;
      job_args[index].data = tmp_buffer_ + offset;
      job_args[index].slba = slba;
      job_args[index].num_block = num;
      job_args[index].context.lba = &(data_seg_start_[seg_num_]);
      job_args[index].context.share = &share;
      share.unfinish_op.fetch_add(1);

      // printf("app arg: %llx %d  \n", job_args[index].slba, num);

      int rc = FackZNSDevice::GetInstance()->AppAppend(&job_args[index]);
      if (rc != 0) {
        printf("write data to zns failed.  slba:%lx num:%d\n", job_args[index].slba, num);
      }

      index++;
      seg_num_++;
      num_block -= num;
      offset += num * kBlockSize;
      assert(offset + kMaxTransmit * kBlockSize < kMaxFileSize);
    }
    share.closed = true;  // 停止写入
    if (share.unfinish_op.load() > 0) {
      share.sem.Wait();
    }

    cur_max_lba = std::max(cur_max_lba, data_seg_start_[seg_num_ - 1]);
    // printf("end request. max_lba:%llx\n", cur_max_lba);
    printf("curr zone:%lx\n", PickZone(cur_max_lba));
  }
}

Status SequentialFileImpl::Read(size_t n, Slice* result, char* scratch) {
  Status s = file_->Read(pos_, n, result, scratch);
  if (s.ok()) {
    pos_ += result->size();
  }
  return s;
}

Status SequentialFileImpl::Skip(uint64_t n) {
  if (pos_ > file_->Size()) {
    return Status::IOError("pos_ > file_->Size()");
  }
  const uint64_t available = file_->Size() - pos_;
  if (n > available) {
    n = available;
  }
  pos_ += n;
  return Status::OK();
}

FakeZnsSpdkEnv::FakeZnsSpdkEnv(Env* base_env) : EnvWrapper(base_env) { printf("ZNS SPDK Env init complete\n"); }

FakeZnsSpdkEnv::~FakeZnsSpdkEnv() {
  for (const auto& kvp : file_map_) {
    kvp.second->Unref();
  }

  printf("ZNS SPDK Env destroy complete\n");
}

// Partial implementation of the Env interface.
Status FakeZnsSpdkEnv::NewSequentialFile(const std::string& fname, SequentialFile** result) {
  MutexLock lock(&mutex_);
  if (file_map_.find(fname) == file_map_.end()) {  // 文件不存在则报错
    *result = nullptr;
    return Status::IOError(fname, "File not found");
  }

  *result = new SequentialFileImpl(file_map_[fname]);
  return Status::OK();
}

Status FakeZnsSpdkEnv::NewRandomAccessFile(const std::string& fname, RandomAccessFile** result) {
  MutexLock lock(&mutex_);
  if (file_map_.find(fname) == file_map_.end()) {  // 文件不存在则报错
    *result = nullptr;
    return Status::IOError(fname, "File not found");
  }

  *result = new RandomAccessFileImpl(file_map_[fname]);
  return Status::OK();
}

Status FakeZnsSpdkEnv::NewWritableFile(const std::string& fname, WritableFile** result) {
  MutexLock lock(&mutex_);
  FileSystem::iterator it = file_map_.find(fname);

  FileState* file;
  if (it == file_map_.end()) {  // 文件不存在则创建新文件
    // File is not currently open.
    file = new FileState(fname);
    file->Ref();
    file_map_[fname] = file;
  } else {  // 文件存在则清空文件内容
    file = it->second;
    file->Truncate();
  }

  *result = new WritableFileImpl(file);
  return Status::OK();
}

Status FakeZnsSpdkEnv::NewAppendableFile(const std::string& fname, WritableFile** result) {
  MutexLock lock(&mutex_);
  FileState** sptr = &file_map_[fname];
  FileState* file = *sptr;
  if (file == nullptr) {  // 文件不存在则创建新文件
    file = new FileState(fname);
    file->Ref();
  }
  *result = new WritableFileImpl(file);
  return Status::OK();
}

bool FakeZnsSpdkEnv::FileExists(const std::string& fname) {
  MutexLock lock(&mutex_);
  return file_map_.find(fname) != file_map_.end();
}

Status FakeZnsSpdkEnv::GetChildren(const std::string& dir, std::vector<std::string>* result) {
  MutexLock lock(&mutex_);
  result->clear();

  for (const auto& kvp : file_map_) {
    const std::string& filename = kvp.first;
    // 比对文件名前缀是否与给定目录一致，返回文件名而不是完整的路径
    if (filename.size() >= dir.size() + 1 && filename[dir.size()] == '/' && Slice(filename).starts_with(Slice(dir))) {
      result->push_back(filename.substr(dir.size() + 1));
    }
  }

  return Status::OK();
}

void FakeZnsSpdkEnv::RemoveFileInternal(const std::string& fname) EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  if (file_map_.find(fname) == file_map_.end()) {
    return;
  }

  file_map_[fname]->Unref();
  file_map_.erase(fname);
}

Status FakeZnsSpdkEnv::RemoveFile(const std::string& fname) {
  MutexLock lock(&mutex_);
  if (file_map_.find(fname) == file_map_.end()) {
    return Status::IOError(fname, "File not found");
  }

  RemoveFileInternal(fname);
  return Status::OK();
}

Status FakeZnsSpdkEnv::GetFileSize(const std::string& fname, uint64_t* file_size) {
  MutexLock lock(&mutex_);
  if (file_map_.find(fname) == file_map_.end()) {
    return Status::IOError(fname, "File not found");
  }

  *file_size = file_map_[fname]->Size();
  return Status::OK();
}

Status FakeZnsSpdkEnv::RenameFile(const std::string& src, const std::string& target) {
  MutexLock lock(&mutex_);
  printf("filename from %s to %s\n", src.c_str(), target.c_str());
  if (file_map_.find(src) == file_map_.end()) {
    return Status::IOError(src, "File not found");
  }

  RemoveFileInternal(target);
  file_map_[target] = file_map_[src];
  file_map_.erase(src);
  return Status::OK();
}

Env* Env::NewFakeZnsSpdkEnv(Env* base_env) { return new FakeZnsSpdkEnv(base_env); }

}  // namespace leveldb
