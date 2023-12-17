#include "zns_spdk_env/filesystem.h"

namespace leveldb {
// FileStates are reference counted. The initial reference count is zero

const bool kZNSRWTest = false;
const bool kDataCmpTest = false;

FileState::FileState() : refs_(0), size_(0) {
  static uint64_t total_mem_used = 0;
  total_mem_used += kMaxFileSize;
  mem_buffer_ = new char[kMaxFileSize];
  spdk_buffer_ = static_cast<char*>(spdk_dma_zmalloc(kMaxFileSize, 1, NULL));
  if (!spdk_buffer_) {
    SPDK_ERRLOG("Failed to allocate buffer\n");
    printf("total memory use:%lxMB\n", total_mem_used / 1024 / 1024);
    return;
  }
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

Status FileState::Read(uint64_t offset, size_t n, Slice* result,
                       char* scratch) const {
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
  if (kZNSRWTest == true) {
    memset(spdk_buffer_, 0x0, kMaxFileSize);
    SpdkContext context;
    context.unfinish_op = block_addrs_.size();  // 待完成的读操作
    int offset = 0;
    for (auto info : block_addrs_) {
      int rc =
          SpdkApi::AppRead(spdk_buffer_ + offset, info.start_block,
                           info.num_block, &context);  // 顺序读取各部分数据
      if (rc != 0) {
        printf("read zns data failed.  lba:%lx num:%d\n", info.start_block,
               info.num_block);
      }
      offset += info.num_block * kBlockSize;
      assert(offset < kMaxFileSize);
    }
    if (context.unfinish_op.load() > 0) {  // 等待读取操作全部完成
      context.sem.Wait();
    }
    if (kDataCmpTest == true) {
      int cmp_res = memcmp(spdk_buffer_, mem_buffer_, size_);
      if (cmp_res != 0) {
        for (int i = 0; i < size_; i++) {
          if (spdk_buffer_[i] != mem_buffer_[i]) {
            printf("start index:%d  block:%d spdk buffer %x mem buffer %x\n", i,
                   i / kBlockSize, spdk_buffer_[i], mem_buffer_[i]);
            break;
          }
        }
        for (int i = size_; i >= 0; i--) {
          if (spdk_buffer_[i] != mem_buffer_[i]) {
            printf("end index:%d  block:%d spdk buffer %x mem buffer %x\n", i,
                   i / kBlockSize, spdk_buffer_[i], mem_buffer_[i]);
            break;
          }
        }
        assert(0);
      }
    }
  }
}

blk_addr_t FileState::PickZone() { return 0x0; }

void FileState::WriteToZNS() {
  if (kZNSRWTest == true) {
    memcpy(spdk_buffer_, mem_buffer_, size_);  // 将数据往对比缓冲区备份一份

    int offset = 0;
    int num_block = (size_ + kBlockSize - 1) / kBlockSize;  // 向上取整
    std::vector<SpdkContext> unfinsh_context(num_block);
    int ret;
    int index = 0;
    while (num_block > 0) {
      int num = std::min(num_block, kMaxTransmit);
      if (num > 0) {
        unfinsh_context[index].unfinish_op = 1;
        blk_addr_t slba = PickZone();
        int rc = SpdkApi::AppWrite(spdk_buffer_ + offset, slba, num,
                                   &unfinsh_context[index]);
        if (rc != 0) {
          printf("write data to zns failed.  slba:%lx num:%d\n", slba, num);
        }
        index++;
        block_addrs_.emplace_back(0x0, num);
      }
      num_block -= num;
      offset += num * kBlockSize;
    }
    for (int i = 0; i < block_addrs_.size(); i++) {
      if (unfinsh_context[i].unfinish_op.load() > 0) {  // 等待写操作执行完成
        unfinsh_context[i].sem.Wait();
      }
      block_addrs_[i].start_block = unfinsh_context[i].lba;  // 记录对应的LBA
    }
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

ZnsSpdkEnv::ZnsSpdkEnv(Env* base_env) : EnvWrapper(base_env) {
  auto spdk_func = [&]() { SpdkApi::AppStart(&spdk_app_context_); };
  spdk_app_thread_ = std::thread(spdk_func);  // 创建线程，初始化SPDK
  spdk_app_context_.sem.Wait();               // 等待SPDK初始化完成
  printf("ZNS SPDK Env init complete\n");
}

ZnsSpdkEnv::~ZnsSpdkEnv() {
  for (const auto& kvp : file_map_) {
    kvp.second->Unref();
  }
  SpdkApi::AppStop();
  spdk_app_thread_.join();
  printf("ZNS SPDK Env destroy complete\n");
}

// Partial implementation of the Env interface.
Status ZnsSpdkEnv::NewSequentialFile(const std::string& fname,
                                     SequentialFile** result) {
  MutexLock lock(&mutex_);
  if (file_map_.find(fname) == file_map_.end()) {  // 文件不存在则报错
    *result = nullptr;
    return Status::IOError(fname, "File not found");
  }

  *result = new SequentialFileImpl(file_map_[fname]);
  return Status::OK();
}

Status ZnsSpdkEnv::NewRandomAccessFile(const std::string& fname,
                                       RandomAccessFile** result) {
  MutexLock lock(&mutex_);
  if (file_map_.find(fname) == file_map_.end()) {  // 文件不存在则报错
    *result = nullptr;
    return Status::IOError(fname, "File not found");
  }

  *result = new RandomAccessFileImpl(file_map_[fname]);
  return Status::OK();
}

Status ZnsSpdkEnv::NewWritableFile(const std::string& fname,
                                   WritableFile** result) {
  MutexLock lock(&mutex_);
  FileSystem::iterator it = file_map_.find(fname);

  FileState* file;
  if (it == file_map_.end()) {  // 文件不存在则创建新文件
    // File is not currently open.
    file = new FileState();
    file->Ref();
    file_map_[fname] = file;
  } else {  // 文件存在则清空文件内容
    file = it->second;
    file->Truncate();
  }

  *result = new WritableFileImpl(file);
  return Status::OK();
}

Status ZnsSpdkEnv::NewAppendableFile(const std::string& fname,
                                     WritableFile** result) {
  MutexLock lock(&mutex_);
  FileState** sptr = &file_map_[fname];
  FileState* file = *sptr;
  if (file == nullptr) {  // 文件不存在则创建新文件
    file = new FileState();
    file->Ref();
  }
  *result = new WritableFileImpl(file);
  return Status::OK();
}

bool ZnsSpdkEnv::FileExists(const std::string& fname) {
  MutexLock lock(&mutex_);
  return file_map_.find(fname) != file_map_.end();
}

Status ZnsSpdkEnv::GetChildren(const std::string& dir,
                               std::vector<std::string>* result) {
  MutexLock lock(&mutex_);
  result->clear();

  for (const auto& kvp : file_map_) {
    const std::string& filename = kvp.first;
    // 比对文件名前缀是否与给定目录一致，返回文件名而不是完整的路径
    if (filename.size() >= dir.size() + 1 && filename[dir.size()] == '/' &&
        Slice(filename).starts_with(Slice(dir))) {
      result->push_back(filename.substr(dir.size() + 1));
    }
  }

  return Status::OK();
}

void ZnsSpdkEnv::RemoveFileInternal(const std::string& fname)
    EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  if (file_map_.find(fname) == file_map_.end()) {
    return;
  }

  file_map_[fname]->Unref();
  file_map_.erase(fname);
}

Status ZnsSpdkEnv::RemoveFile(const std::string& fname) {
  MutexLock lock(&mutex_);
  if (file_map_.find(fname) == file_map_.end()) {
    return Status::IOError(fname, "File not found");
  }

  RemoveFileInternal(fname);
  return Status::OK();
}

Status ZnsSpdkEnv::GetFileSize(const std::string& fname, uint64_t* file_size) {
  MutexLock lock(&mutex_);
  if (file_map_.find(fname) == file_map_.end()) {
    return Status::IOError(fname, "File not found");
  }

  *file_size = file_map_[fname]->Size();
  return Status::OK();
}

Status ZnsSpdkEnv::RenameFile(const std::string& src,
                              const std::string& target) {
  MutexLock lock(&mutex_);
  if (file_map_.find(src) == file_map_.end()) {
    return Status::IOError(src, "File not found");
  }

  RemoveFileInternal(target);
  file_map_[target] = file_map_[src];
  file_map_.erase(src);
  return Status::OK();
}

Env* Env::NewZnsSpdk(Env* base_env) { return new ZnsSpdkEnv(base_env); }

}  // namespace leveldb
