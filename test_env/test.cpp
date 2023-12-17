#include <cassert>
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <leveldb/env.h>

std::string RandStr(int min_len,
                    int max_len) {  // 生成min_len-max_len之间大小的字符串
  int len = rand() % (max_len - min_len + 1) + min_len;
  std::string str;
  char c;
  int idx;
  /*循环向字符串中添加随机生成的字符*/
  for (idx = 0; idx < len; idx++) {
    c = 'a' + rand() % 26;
    str.push_back(c);
  }
  return str;
}

std::string RandStr(int len) {  // 生成min_len-max_len之间大小的字符串
  std::string str;
  char c;
  int idx;
  /*循环向字符串中添加随机生成的字符*/
  for (idx = 0; idx < len; idx++) {
    c = 'a' + rand() % 26;
    str.push_back(c);
  }
  return str;
}

void PrintStats(leveldb::DB* db, std::string key) {
  std::string stats;
  if (!db->GetProperty(key, &stats)) {
    stats = "(failed)";
  }
  std::cout << key << std::endl << stats << std::endl;
}

int main() {
  leveldb::DB* db;
  leveldb::Options options;
  options.env =
      leveldb::Env::NewZnsSpdk(leveldb::Env::Default());  // spdk env实现
  options.create_if_missing = true;  // 不存在时创建数据库
  leveldb::Status status = leveldb::DB::Open(options, "./testdb", &db);
  assert(status.ok());
  printf("leveldb open complete.\n");
  std::vector<std::string> keys;  // kv测试数据
  int key_num = 1 << 22;
  for (int i = 0; i < key_num; i++) {
    keys.emplace_back(RandStr(16));
  }
  std::string test_value = std::string('a', 25);
  auto start = std::chrono::system_clock::now();
  printf("write part.\n");
  for (auto& key : keys) {  // 写入数据
    leveldb::Status s = db->Put(leveldb::WriteOptions(), key, test_value);
    assert(s.ok());
  }

  printf("read part.\n");
  for (int k = 0; k <= 10; k++) {
    std::random_shuffle(keys.begin(), keys.end());  // 打乱数据顺序
    for (int i = 0; i < key_num / 10; i++) {        // 读取验证
      std::string value;
      leveldb::Status s = db->Get(leveldb::ReadOptions(), keys[i], &value);
      assert(s.ok());
      assert(value == test_value);
    }
  }

  auto end = std::chrono::system_clock::now();
  printf("用时:%lds\n",
         std::chrono::duration_cast<std::chrono::seconds>(end - start).count());
  PrintStats(db, "leveldb.num-files-at-level0");
  PrintStats(db, "leveldb.num-files-at-level1");
  PrintStats(db, "leveldb.num-files-at-level2");
  PrintStats(db, "leveldb.num-files-at-level3");
  PrintStats(db, "leveldb.num-files-at-level4");
  PrintStats(db, "leveldb.num-files-at-level5");
  PrintStats(db, "leveldb.num-files-at-level6");
  PrintStats(db, "leveldb.stats");
  PrintStats(db, "leveldb.approximate-memory-usage");
  PrintStats(db, "leveldb.sstables");
  delete db;
  delete options.env;
  return 0;
}
