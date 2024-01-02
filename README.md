相关博客：[zns-spdk-env](https://www.jiasun.top/continue/zns-spdk-env.html)

zns_spdk_env目录代码实现ZnsSpdkEnv

test_env目录代码简单测试ZnsSpdkEnv



spdk第三方库的安装参照博客动态库编译的步骤

```bash
./configure --enable-debug --with-shared
make 
make install # 实际上可以不安装到系统目录，不过我没有使用-I指定头文件目录，所以使用make install省点事
# 使用ubuntu下直接在/etc/ld.so.conf.d下加入spdk.conf，添加spdk与dpdk动态库搜索路径
# 使用ldconfig更新，并使用ldconfig -v查看路径是否添加成功
```

![](https://jiasun-blog.oss-cn-hangzhou.aliyuncs.com/blog/202401020947520.png)





leveldb按照github步骤编译第三方库即可

```bash
cmake -DCMAKE_BUILD_TYPE=Debug .. && cmake --build .
make 
make install
```





**TODO**

1. 将中间的普通内存去掉，保证数据读写的准确性

2. 完善下发写请求时Zone的选择机制

3. 写入一定数据后关闭对应Zone

   
