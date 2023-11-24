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
  printf("leveldb open complete.\n");
  std::vector<std::pair<std::string, std::string>> data;  // kv测试数据
  int data_size = 1 << 24;
  for (int i = 0; i < data_size; i++) {
    data.emplace_back(RandStr(10, 30), RandStr(10, 30));
  }
  auto start = std::chrono::system_clock::now();
  printf("write part.\n");
  for (auto& kv : data) {  // 写入数据
    leveldb::Status s = db->Put(leveldb::WriteOptions(), kv.first, kv.second);
    assert(status.ok());
  }
  std::random_shuffle(data.begin(), data.end());  // 打乱数据顺序
  printf("read part.\n");
  for (int k = 0; k <= 10; k++) {
    for (int i = 0; i < data_size / 10; i++) {  // 读取验证
      std::string value;
      leveldb::Status s =
          db->Get(leveldb::ReadOptions(), data[i].first, &value);
      assert(status.ok());
      assert(value == data[i].second);
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
  return 0;
}
