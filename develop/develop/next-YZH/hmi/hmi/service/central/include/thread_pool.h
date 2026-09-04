#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "central_server.h"

// 初始化线程池
int thread_pool_init(int thread_count, int queue_size,
                     on_data_received_t on_data,
                     on_device_online_t on_online,
                     on_device_offline_t on_offline,
                     on_heartbeat_t on_heartbeat);

// 提交数据到队列（非阻塞）
void thread_pool_submit(ClientConnection *conn, DataPayload *payload);

// 销毁线程池
void thread_pool_destroy(void);

#endif