#ifndef CENTRAL_SERVER_H
#define CENTRAL_SERVER_H

#include <stdint.h>
#include <time.h>
#include "proto.h"

#define RECV_BUF_SIZE 4096
#define MAX_CLIENTS 100
#define HEARTBEAT_TIMEOUT 15  // 秒

/* ========== 客户端连接上下文 ========== */
typedef struct {
    int fd;                      // socket fd
    uint16_t device_id;          // 设备号（解析后填充）
    char recv_buf[RECV_BUF_SIZE]; // 接收缓冲区
    int recv_len;                // 当前缓冲有效数据长度
    time_t last_heartbeat;       // 最后心跳时间
    int online;                  // 1=在线, 0=离线
    char ip[16];                 // 客户端IP
    uint16_t port;               // 客户端端口
    time_t connected_time;       // 连接建立时间
    uint64_t packet_count;       // 收到包计数（统计用）
} ClientConnection;

/* ========== 回调函数类型（由上层注册） ========== */

// 收到数据上报包时调用
typedef void (*on_data_received_t)(ClientConnection *conn, DataPayload *data);

// 设备上线时调用（首次收到心跳或数据）
typedef void (*on_device_online_t)(ClientConnection *conn);

// 设备离线时调用（心跳超时或连接断开）
typedef void (*on_device_offline_t)(ClientConnection *conn);

// 收到心跳包时调用
typedef void (*on_heartbeat_t)(ClientConnection *conn);

/* ========== 服务端控制接口 ========== */

// 初始化服务器
// port: 监听端口
// on_data: 数据回调（可为NULL）
// on_online: 上线回调（可为NULL）  
// on_offline: 离线回调（可为NULL）
// on_heartbeat: 心跳回调（可为NULL）
// 返回值: 0成功, -1失败
int server_init(int port, 
                on_data_received_t on_data,
                on_device_online_t on_online,
                on_device_offline_t on_offline,
                on_heartbeat_t on_heartbeat);

// 运行事件循环（阻塞，直到收到停止信号）
void server_run(void);

// 停止服务器
void server_stop(void);

// 获取客户端连接数
int server_get_client_count(void);

// 根据设备号查找连接
ClientConnection* server_find_by_device_id(uint16_t device_id);

// 断开指定连接
void server_disconnect_client(int fd);

#endif