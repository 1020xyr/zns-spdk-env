all: normal fake
test:test.cpp
	g++ test.cpp -o test -pthread -g  -o test  -lleveldb -Wl,--no-as-needed  -I/usr/local/include -L/usr/local/lib -L/root/spdk/dpdk/build/lib -L/usr/local/lib -lspdk_env_dpdk -lrte_eal -lrte_mempool -lrte_ring -lrte_mbuf -lrte_bus_pci -lrte_pci -lrte_mempool_ring -lrte_telemetry -lrte_kvargs -lrte_rcu -lrte_power -lrte_ethdev -lrte_vhost -lrte_net -lrte_dmadev -lrte_cryptodev -lrte_hash -lspdk_event_iscsi -lspdk_event_nbd -lspdk_nbd -lspdk_event_nvmf -lspdk_nvmf -lspdk_event_scheduler -lspdk_event -lspdk_env_dpdk_rpc -lspdk_event_scsi -lspdk_event_bdev -lspdk_event_accel -lspdk_event_iobuf -lspdk_event_sock -lspdk_event_vmd -lspdk_init -lspdk_vhost -lspdk_iscsi -lspdk_conf -lspdk_scsi -lspdk_blobfs_bdev -lspdk_blob_bdev -lspdk_bdev -lspdk_notify -lspdk_bdev_malloc -lspdk_bdev_null -lspdk_bdev_nvme -lspdk_accel -lspdk_accel_error -lspdk_accel_ioat -lspdk_ioat -lspdk_bdev_passthru -lspdk_bdev_lvol -lspdk_bdev_raid -lspdk_bdev_error -lspdk_bdev_gpt -lspdk_bdev_split -lspdk_bdev_delay -lspdk_bdev_zone_block -lspdk_lvol -lspdk_nvme -lspdk_sock -lspdk_sock_posix -lspdk_bdev_aio -lspdk_bdev_ftl -lspdk_ftl -lspdk_bdev_virtio -lspdk_virtio -lspdk_vfio_user -lspdk_blobfs -lspdk_blob -lspdk_dma -lspdk_vmd -lspdk_thread -lspdk_trace -lspdk_rpc -lspdk_jsonrpc -lspdk_json -lspdk_util -lspdk_log  -Wl,--as-needed

normal:normal.cpp
	g++ normal.cpp -o normal -pthread -g    -lleveldb 

fake:fake.cpp
	g++ fake.cpp -o fake -pthread -g    -lleveldb 
clean:
	rm fake normal
