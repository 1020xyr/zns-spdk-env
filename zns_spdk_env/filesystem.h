#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include <thread>

#include "leveldb/env.h"
#include "leveldb/status.h"
#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/mutexlock.h"
#include "zns_spdk_env/spdk_api.h"

namespace leveldb {
using blk_addr_t = uint64_t;  // 块号类型
struct BlockInfo {
  blk_addr_t start_block;  // 起始块
  int num_block;           // 块数
  BlockInfo(blk_addr_t lba, int num) : start_block(lba), num_block(num) {}
  BlockInfo() = default;
};

const int kMaxFileSize = 2 * 1024 * 1024;
const int kMaxTransmit = 32;
class FileState {
 public:
  // FileStates are reference counted. The initial reference count is zero
  // and the caller must call Ref() at least once.
  FileState();

  // No copying allowed.
  FileState(const FileState&) = delete;
  FileState& operator=(const FileState&) = delete;

  // Increase the reference count.
  void Ref() {
    std::lock_guard<std::mutex> lk(refs_mutex_);
    ++refs_;
  }

  // Decrease the reference count. Delete if this is the last reference.
  void Unref();

  uint64_t Size() const {
    std::lock_guard<std::mutex> lk(blocks_mutex_);
    return size_;
  }

  void Truncate() {
    std::lock_guard<std::mutex> lk(blocks_mutex_);
    spdk_dma_free(buffer_);  // 释放内存缓冲区
    size_ = 0;
  }

  Status Read(uint64_t offset, size_t n, Slice* result, char* scratch) const;

  Status Append(const Slice& data);

  void ReadFromZNS();  // 从ZNS中读取文件内容
  void WriteToZNS();   // 向ZNS中写入文件内容

 private:
  enum { kBlockSize = 4 * 1024 };

  // Private since only Unref() should be used to delete it.
  ~FileState() { Truncate(); }
  blk_addr_t PickZone();

  std::mutex refs_mutex_;
  int refs_;

  mutable std::mutex blocks_mutex_;
  char* buffer_;  // 读缓冲区
  uint64_t size_;
  std::vector<BlockInfo> block_addrs_;  // 块地址
};

class SequentialFileImpl : public SequentialFile {
 public:
  explicit SequentialFileImpl(FileState* file) : file_(file), pos_(0) {
    file_->Ref();
    file_->ReadFromZNS();  // 读文件内容前将数据从ZNS SSD中读取出来
  }

  ~SequentialFileImpl() override { file_->Unref(); }

  Status Read(size_t n, Slice* result, char* scratch) override;

  Status Skip(uint64_t n) override;

 private:
  FileState* file_;
  uint64_t pos_;  // 顺序读，记录读取下标
};

class RandomAccessFileImpl : public RandomAccessFile {
 public:
  explicit RandomAccessFileImpl(FileState* file) : file_(file) {
    file_->Ref();
    file_->ReadFromZNS();  // 读文件内容前将数据从ZNS SSD中读取出来
  }

  ~RandomAccessFileImpl() override { file_->Unref(); }

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    return file_->Read(offset, n, result, scratch);
  }

 private:
  FileState* file_;
};

class WritableFileImpl : public WritableFile {
 public:
  WritableFileImpl(FileState* file) : file_(file) { file_->Ref(); }

  ~WritableFileImpl() override { file_->Unref(); }

  Status Append(const Slice& data) override { return file_->Append(data); }

  Status Close() override {
    file_->WriteToZNS();  // 文件写完成后将数据写入到ZNS SSD中并记录LBA
    return Status::OK();
  }
  Status Flush() override { return Status::OK(); }
  Status Sync() override { return Status::OK(); }

 private:
  FileState* file_;
};

class ZnsSpdkEnv : public EnvWrapper {
 public:
  explicit ZnsSpdkEnv(Env* base_env);

  ~ZnsSpdkEnv() override;

  // Partial implementation of the Env interface.
  Status NewSequentialFile(const std::string& fname,
                           SequentialFile** result) override;

  Status NewRandomAccessFile(const std::string& fname,
                             RandomAccessFile** result) override;

  Status NewWritableFile(const std::string& fname,
                         WritableFile** result) override;

  Status NewAppendableFile(const std::string& fname,
                           WritableFile** result) override;

  bool FileExists(const std::string& fname) override;

  Status GetChildren(const std::string& dir,
                     std::vector<std::string>* result) override;

  void RemoveFileInternal(const std::string& fname)
      EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  Status RemoveFile(const std::string& fname) override;

  Status CreateDir(const std::string& dirname) override { return Status::OK(); }

  Status RemoveDir(const std::string& dirname) override { return Status::OK(); }

  Status GetFileSize(const std::string& fname, uint64_t* file_size) override;

  Status RenameFile(const std::string& src, const std::string& target) override;

  Status LockFile(const std::string& fname, FileLock** lock) override {
    *lock = new FileLock;
    return Status::OK();
  }

  Status UnlockFile(FileLock* lock) override {
    delete lock;
    return Status::OK();
  }

  Status GetTestDirectory(std::string* path) override {
    *path = "/test";
    return Status::OK();
  }

 private:
  // Map from filenames to FileState objects, representing a simple file system.
  typedef std::map<std::string, FileState*> FileSystem;

  port::Mutex mutex_;
  FileSystem file_map_ GUARDED_BY(mutex_);
  std::thread spdk_app_thread_;
  SpdkContext spdk_app_context_;
};

};  // namespace leveldb