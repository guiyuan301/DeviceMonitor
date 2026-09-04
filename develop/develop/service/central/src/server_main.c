/* ============================================================================
 * server_main.c — 中央服务器启动入口与业务回调实现
 * ============================================================================
 * 【职责】
 *  1. 实现4个业务回调函数：数据入库、设备上线/离线状态更新、心跳日志
 *  2. main() 中按顺序初始化各模块，然后启动事件循环
 *
 * 【启动顺序】
 *  db_init → server_init(注册回调) → thread_pool_init → server_run(阻塞)
 *  收到 SIGINT/SIGTERM 后依次销毁：thread_pool → server → db
 *
 * 【数据流向（一次完整采样）】
 *  采集板 TCP 发包
 *    → central_server.c 的 epoll 事件循环 recv()
 *    → protocol_parser.c 解析出 DataPayload
 *    → thread_pool.c 入队，工作线程取出
 *    → 调用本文件的 on_data_received()
 *    → db_insert_measurement() 写入 SQLite
 * ============================================================================
 */

/* ========== 头文件 ========== */
#include "../include/central_server.h"   // 服务端接口、ClientConnection、回调类型定义
#include "../include/thread_pool.h"      // 线程池接口：thread_pool_init / submit / destroy
#include "../include/database_manager.h" // 数据库接口：db_init / db_close / db_insert_measurement / db_update_device_status
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* ============================================================================
 * 一、业务回调函数
 *    这4个函数由 central_server.c 在适当时机调用：
 *      on_data_received  — 收到 type=0x01 数据包后，由工作线程回调
 *      on_device_online  — 设备首次收到有效包或从离线恢复时回调
 *      on_device_offline — 心跳超时或 TCP 断开时回调
 *      on_heartbeat      — 收到 type=0x02 心跳包时回调
 *    回调在工作线程中执行，不是主线程。
 * ============================================================================ */

/**
 * @brief 数据上报回调 —— 将采集数据写入数据库并转发给HMI
 * @param conn  当前连接上下文（含 device_id、IP 等）
 * @param data  解析后的负载（温度、湿度、状态、产量、时间戳）
 *
 * 调用链：thread_pool worker_thread → on_data_received → db_insert_measurement
 * 注意：本函数在工作线程中执行，多线程并发调用时依赖 database_manager 内部的互斥锁。
 */
static void on_data_received(ClientConnection *conn, DataPayload *data) {
    // 打印日志，方便调试（temperature 单位是 ×100，除以100还原为实际℃）
    printf("[DATA] 设备[%d] 温度:%.2f℃ 湿度:%d%% 状态:%s 产量:%d 时间:%u\n",
           conn->device_id,
           data->temperature / 100.0,
		   data->humi,   //打印湿度
           data->status ? "运行" : "停机",
           data->production,
           data->timestamp);

    // 调用数据库接口，将数据写入历史表、实时表，并跑告警判定
    if (dm_insert_measurement(conn->device_id, data) != 0) {
        fprintf(stderr, "[WARN] 设备[%d] 数据入库失败\n", conn->device_id);
    }
    
    // 转发实时数据给所有HMI看板客户端（携带设备号，HMI据此识别设备）
    server_broadcast_to_hmi(0x01, conn->device_id, data, sizeof(DataPayload));
}

/**
 * @brief 设备上线回调 —— 更新数据库中设备在线状态为 1
 * @param conn  当前连接上下文
 *
 * 触发时机：设备首次发送有效数据包或心跳包，或者从离线状态恢复连接。
 * 如果是HMI看板客户端注册，推送数据库中的设备列表和实时数据。
 */

// HMI设备信息推送回调
static void push_device_info(const device_info_t *dev, void *ud) {
    ClientConnection *hmi = (ClientConnection *)ud;
    DeviceInfoPayload info;
    memset(&info, 0, sizeof(info));
    info.device_id = dev->device_id;
    strncpy(info.name, dev->name, sizeof(info.name) - 1);
    strncpy(info.group_name, dev->group_name, sizeof(info.group_name) - 1);
    
    // 查询设备在线状态
    sensor_sample_t s;
    if (dm_query_realtime(dev->device_id, &s) == 0) {
        info.online = 1;
    } else {
        info.online = 0;
    }
    
    server_send_to_hmi(hmi, 0x10, dev->device_id, &info, sizeof(info));
}

// HMI实时数据推送回调
static void push_realtime_data(const sensor_sample_t *sample, void *ud) {
    ClientConnection *hmi = (ClientConnection *)ud;
    DataPayload data;
    memset(&data, 0, sizeof(data));
    data.timestamp = sample->sample_ts;
    data.temperature = (int16_t)(sample->temperature * 100);
    data.humi = (uint8_t)sample->humidity;
    data.status = sample->run_status;
    data.production = (int32_t)sample->output_count;
    
    server_send_to_hmi(hmi, 0x01, sample->device_id, &data, sizeof(data));
}

static void on_device_online(ClientConnection *conn) {
    printf("[ONLINE] 设备[%d] 上线 (ip=%s:%d)\n", 
           conn->device_id, conn->ip, conn->port);
    
    // HMI看板客户端注册：推送数据库中的设备列表和实时数据
    if (conn->is_hmi) {
        printf("[HMI] 开始向HMI推送设备数据...\n");
        
        // 推送所有设备信息
        dm_foreach_devices(push_device_info, conn);
        
        // 推送所有设备的实时数据
        dm_foreach_realtime(push_realtime_data, conn);
        
        printf("[HMI] 设备数据推送完成\n");
    }
    
    dm_update_device_status(conn->device_id, 1); // 1 = 在线
}

/**
 * @brief 设备离线回调 —— 更新数据库中设备在线状态为 0
 * @param conn  当前连接上下文
 *
 * 触发时机：心跳超时（15秒未收到任何包）或 TCP 连接断开。
 * 调用链：central_server.c remove_client → on_device_offline → db_update_device_status
 */
static void on_device_offline(ClientConnection *conn) {
    printf("[OFFLINE] 设备[%d] 离线 (ip=%s:%d)\n",
           conn->device_id, conn->ip, conn->port);
    dm_update_device_status(conn->device_id, 0); // 0 = 离线
}

/**
 * @brief 心跳回调 —— 仅打印日志，刷新连接的 last_heartbeat 时间戳
 * @param conn  当前连接上下文
 *
 * 注意：last_heartbeat 的更新在 central_server.c 中已经完成，
 *       这里只负责业务层面的日志记录（也可扩展为统计在线时长等）。
 */
static void on_heartbeat(ClientConnection *conn) {
    printf("[HEARTBEAT] 设备[%d] 心跳\n", conn->device_id);
}

/* ============================================================================
 * 二、main 函数 —— 程序入口，按顺序初始化各模块
 * ============================================================================ */

int main(int argc, char *argv[]) {
    /* ---------- 1. 解析命令行参数 ---------- */
    int port = 8888;  // 默认监听端口
    
    if (argc >= 2) {
        port = atoi(argv[1]);  // 支持 ./server <port> 自定义端口
    }
    
    /* ---------- 2. 打印启动信息 ---------- */
    printf("========== 中央服务器启动 ==========\n");
    printf("监听端口: %d\n", port);
    printf("最大连接数: %d\n", MAX_CLIENTS);        // 定义在 central_server.h，值为100
    printf("心跳超时: %d 秒\n", HEARTBEAT_TIMEOUT); // 定义在 central_server.h，值为15
    printf("=====================================\n\n");
    
    /* ---------- 3. 初始化数据库 ---------- */
    // 必须在服务器启动前完成：创建数据库文件，建立 5 张表 + 告警引擎
    // 如果数据库已存在则直接打开，不会清空历史数据
    if (dm_init("production.db") != 0) {
        fprintf(stderr, "数据库初始化失败，程序退出\n");
        return 1;
    }

    /* ---------- 4. 初始化网络服务端 ---------- */
    // 创建 TCP 监听 socket → 绑定端口 → 注册4个回调函数 → 进入 epoll 等待
    // 回调函数会在设备收发数据时被 central_server.c 调用
    // 注意：此时还未进入事件循环，只是完成初始化
    if (server_init(port, 
                    on_data_received,     // 数据回调：收到数据包时调用
                    on_device_online,     // 上线回调：设备上线时调用
                    on_device_offline,    // 离线回调：设备离线时调用
                    on_heartbeat) < 0) {  // 心跳回调：收到心跳包时调用
        fprintf(stderr, "服务器初始化失败\n");
        dm_close();  // 初始化失败时关闭已打开的数据库，防止资源泄漏
        return 1;
    }
     
    /* ---------- 5. 初始化线程池 ---------- */
    // 创建4个工作线程，任务队列容量1000
    // 线程池同样接收4个回调，工作线程从队列取出任务后调用对应的回调函数
    // 数据流：central_server 提交任务 → 队列 → worker_thread 取出 → 回调入库
	thread_pool_init(4,1000,
					 on_data_received,
					 on_device_online,
					 on_device_offline,
					 on_heartbeat);

    /* ---------- 6. 启动事件循环（阻塞） ---------- */
    // 进入 epoll_wait 循环，持续接收客户端连接和数据
    // 收到 SIGINT(Ctrl+C) 或 SIGTERM 信号后 g_running=0，循环退出
    server_run();

    /* ---------- 7. 优雅退出：按启动的反序释放资源 ---------- */
    thread_pool_destroy();  // 停止工作线程，清空队列，释放 pthread 资源
    server_stop();          // 关闭所有客户端连接、关闭监听 socket、关闭 epoll fd
    dm_close();             // 关闭 SQLite 连接，确保数据写入磁盘
    
    printf("服务器已退出\n");
    return 0;
}