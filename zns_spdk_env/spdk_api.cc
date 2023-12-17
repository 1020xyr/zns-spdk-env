#include "zns_spdk_env/spdk_api.h"
namespace leveldb {

struct SpdkInfo {  // SPDK的一些相关信息
  struct spdk_bdev* bdev;
  struct spdk_bdev_desc* bdev_desc;
  struct spdk_io_channel* bdev_io_channel;
  spdk_thread* app_thread;
};

SpdkInfo g_spdk_info;

static void bdev_event_cb(enum spdk_bdev_event_type type,
                          struct spdk_bdev* bdev, void* event_ctx) {
  SPDK_NOTICELOG("Unsupported bdev event: type %d\n", type);
}

// 重置zone回调函数
static void reset_zone_complete(struct spdk_bdev_io* bdev_io, bool success,
                                void* cb_arg) {
  SpdkContext* context = static_cast<SpdkContext*>(cb_arg);
  spdk_bdev_free_io(bdev_io);

  if (!success) {
    SPDK_ERRLOG("bdev io reset zone error: %d\n", EIO);
    spdk_put_io_channel(g_spdk_info.bdev_io_channel);
    spdk_bdev_close(g_spdk_info.bdev_desc);
    spdk_app_stop(-1);
    return;
  }
  context->unfinish_op.fetch_sub(1);
  if (context->unfinish_op.load() == 0) {  // zone重置完成，开始写入数据
    SPDK_NOTICELOG("spdk init success.\n");
    context->sem.Signal();  // 与调用AppStart线程同步
  }
}

// 打开SPDK Bdev并记录相关信息，重置若干个zone
static void start_fn(void* arg) {
  SpdkContext* context = static_cast<SpdkContext*>(arg);
  uint32_t buf_align;
  int rc = 0;
  const char* bdev_name = "Nvme0n1";
  SPDK_NOTICELOG("Successfully started the application\n");
  SPDK_NOTICELOG("Opening the bdev %s\n", bdev_name);
  rc = spdk_bdev_open_ext(bdev_name, true, bdev_event_cb, NULL,
                          &g_spdk_info.bdev_desc);
  if (rc) {
    SPDK_ERRLOG("Could not open bdev: %s\n", bdev_name);
    spdk_app_stop(-1);
    return;
  }
  g_spdk_info.bdev = spdk_bdev_desc_get_bdev(g_spdk_info.bdev_desc);

  SPDK_NOTICELOG("Opening io channel\n");
  /* Open I/O channel */
  g_spdk_info.bdev_io_channel = spdk_bdev_get_io_channel(g_spdk_info.bdev_desc);
  if (g_spdk_info.bdev_io_channel == NULL) {
    SPDK_ERRLOG("Could not create bdev I/O channel!!\n");
    spdk_bdev_close(g_spdk_info.bdev_desc);
    spdk_app_stop(-1);
    return;
  }
  g_spdk_info.app_thread = spdk_thread_get_app_thread();
  // 打印ZNS SSD一些信息
  SPDK_NOTICELOG(
      "block size:%d write unit:%d zone size:%lx zone num:%ld max append "
      "size:%d max open zone:%d max active "
      "zone:%d\n",
      spdk_bdev_get_block_size(g_spdk_info.bdev),
      spdk_bdev_get_write_unit_size(g_spdk_info.bdev),
      spdk_bdev_get_zone_size(g_spdk_info.bdev),
      spdk_bdev_get_num_zones(g_spdk_info.bdev),
      spdk_bdev_get_max_zone_append_size(g_spdk_info.bdev),
      spdk_bdev_get_max_open_zones(g_spdk_info.bdev),
      spdk_bdev_get_max_active_zones(g_spdk_info.bdev));

  SPDK_NOTICELOG("begin reset zone\n");
  int zone_num = 10;
  uint64_t zone_size = spdk_bdev_get_zone_size(g_spdk_info.bdev);
  context->unfinish_op = zone_num;
  for (uint64_t slba = 0; slba < zone_num * zone_size; slba += zone_size) {
    rc = spdk_bdev_zone_management(
        g_spdk_info.bdev_desc, g_spdk_info.bdev_io_channel, slba,
        SPDK_BDEV_ZONE_RESET, reset_zone_complete, context);
    if (rc != 0) {
      SPDK_ERRLOG("reset zone failed.");
    }
  }
}

// app线程主函数
void SpdkApi::AppStart(SpdkContext* context) {
  struct spdk_app_opts opts = {};
  int rc = 0;

  spdk_app_opts_init(&opts, sizeof(opts));
  opts.name = "test_bdev";
  opts.json_config_file = "zns.json";

  rc = spdk_app_start(&opts, start_fn, context);
  if (rc) {
    SPDK_ERRLOG("ERROR starting application\n");
  }
  SPDK_NOTICELOG("spdk thread exit.\n");
}

// 释放之前申请的资源并停止app例程
void close_bdev(void* arg) {
  SPDK_NOTICELOG("close spdk bdev.\n");
  SpdkInfo* spdk_info = static_cast<SpdkInfo*>(arg);
  spdk_put_io_channel(spdk_info->bdev_io_channel);
  spdk_bdev_close(spdk_info->bdev_desc);
  spdk_app_stop(0);
}

// 结束app线程生命周期
void SpdkApi::AppStop() {
  spdk_thread_send_msg(spdk_thread_get_app_thread(), close_bdev, &g_spdk_info);
}

// 读回调函数
static void ReadCpl(struct spdk_bdev_io* bdev_io, bool success, void* cb_arg) {
  SpdkContext* context = static_cast<SpdkContext*>(cb_arg);
  spdk_bdev_free_io(bdev_io);

  if (!success) {
    SPDK_ERRLOG("bdev io read zone error: %d\n", EIO);
    assert(0);
    return;
  }

  context->unfinish_op.fetch_sub(1);
  if (context->unfinish_op.load() == 0) {  // 所有读操作均已完成
    context->sem.Signal();
  }
}

// 向块设备中读取数据
int SpdkApi::AppRead(char* data, uint64_t lba, int num_block,
                     SpdkContext* context) {
  int rc =
      spdk_bdev_read_blocks(g_spdk_info.bdev_desc, g_spdk_info.bdev_io_channel,
                            data, lba, num_block, ReadCpl, context);
  if (rc != 0) {
    SPDK_ERRLOG("AppRead error %d", rc);
  }
  return rc;
}

// 写回调函数
static void WriteCpl(struct spdk_bdev_io* bdev_io, bool success, void* cb_arg) {
  SpdkContext* context = static_cast<SpdkContext*>(cb_arg);
  // 记录成功append的LBA
  context->lba = spdk_bdev_io_get_append_location(bdev_io);
  spdk_bdev_free_io(bdev_io);
  if (!success) {
    SPDK_ERRLOG("bdev io write zone error: %d\n", EIO);
    assert(0);
    return;
  }
  context->unfinish_op.fetch_sub(1);
  context->sem.Signal();
}

// 向块设备中写入数据
int SpdkApi::AppWrite(char* data, uint64_t slba, int num_block,
                      SpdkContext* context) {
  int rc =
      spdk_bdev_zone_append(g_spdk_info.bdev_desc, g_spdk_info.bdev_io_channel,
                            data, slba, num_block, WriteCpl, context);
  if (rc != 0) {
    SPDK_ERRLOG("AppWrite error %d", rc);
  }
  return rc;
}
}  // namespace leveldb