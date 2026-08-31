#include "../include/central_server.h"
#include "../include/thread_pool.h"
#include "../include/database_manager.h" 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ========== 回调函数实现（示例） ========== */

// 收到数据上报
static void on_data_received(ClientConnection *conn, DataPayload *data) {
    printf("[DATA] 设备[%d] 温度:%.2f℃ 湿度:%d%% 状态:%s 产量:%d 时间:%u\n",
           conn->device_id,
           data->temperature / 100.0,
		   data->humi,   //打印湿度
           data->status ? "运行" : "停机",
           data->production,
           data->timestamp);

    // 【核心修改】调用约定的数据入库接口
    if (db_insert_measurement(conn->device_id, data) != 0) {
        fprintf(stderr, "[WARN] 设备[%d] 数据入库失败\n", conn->device_id);
    }
}

// 设备上线
static void on_device_online(ClientConnection *conn) {
    printf("[ONLINE] 设备[%d] 上线 (ip=%s:%d)\n", 
           conn->device_id, conn->ip, conn->port);
    db_update_device_status(conn->device_id, 1); // 更新为在线
}

// 设备离线
static void on_device_offline(ClientConnection *conn) {
    printf("[OFFLINE] 设备[%d] 离线 (ip=%s:%d)\n",
           conn->device_id, conn->ip, conn->port);
    db_update_device_status(conn->device_id, 0); // 更新为离线
}

// 收到心跳
static void on_heartbeat(ClientConnection *conn) {
    printf("[HEARTBEAT] 设备[%d] 心跳\n", conn->device_id);
}

/* ========== 主函数 ========== */

int main(int argc, char *argv[]) {
    int port = 8888;
    
    if (argc >= 2) {
        port = atoi(argv[1]);
    }
    
    printf("========== 中央服务器启动 ==========\n");
    printf("监听端口: %d\n", port);
    printf("最大连接数: %d\n", MAX_CLIENTS);
    printf("心跳超时: %d 秒\n", HEARTBEAT_TIMEOUT);
    printf("=====================================\n\n");
    
    // 【核心修改】初始化数据库（注意：必须在服务器启动前）
    if (db_init("production.db") != 0) {
        fprintf(stderr, "数据库初始化失败，程序退出\n");
        return 1;
    }

    // 初始化服务器
    if (server_init(port, 
                    on_data_received,
                    on_device_online,
                    on_device_offline,
                    on_heartbeat) < 0) {
        fprintf(stderr, "服务器初始化失败\n");
        db_close();
        return 1;
    }
     
	//初始化线程池（4个工作线程，队列大小1000）
	thread_pool_init(4,1000,
					 on_data_received,
					 on_device_online,
					 on_device_offline,
					 on_heartbeat);

	// 运行事件循环
    server_run();

    // 清理
	thread_pool_destroy();
    server_stop();
    
    // 关闭数据库
    db_close();
    
    printf("服务器已退出\n");
    return 0;
}