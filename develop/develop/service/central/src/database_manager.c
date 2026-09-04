/*
 * database_manager.c — 桥接层：用 storage + alarm 替换原有简单数据库
 * 所有 storage 调用都加 mutex，保证多线程安全
 */
#include "../include/database_manager.h"
#include "storage.h"
#include "alarm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

static pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ========== 设备ID收集上下文 ========== */
typedef struct {
    uint16_t *ids;
    int count;
    int capacity;
} DeviceIdCollectCtx;

/* ========== 初始化 ========== */

int dm_init(const char *db_path) {
    pthread_mutex_lock(&g_db_mutex);

    if (db_open(db_path) != DB_OK) {
        fprintf(stderr, "[DB ERROR] 数据库打开失败: %s\n", db_path);
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    if (alarm_init() != DB_OK) {
        fprintf(stderr, "[DB ERROR] 告警引擎初始化失败\n");
        db_close();
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }

    printf("[INFO] 数据库初始化成功 (storage + alarm)\n");
    pthread_mutex_unlock(&g_db_mutex);
    return 0;
}

void dm_close(void) {
    pthread_mutex_lock(&g_db_mutex);
    alarm_destroy();
    db_close();
    pthread_mutex_unlock(&g_db_mutex);
}

/* ========== 数据入库 ========== */

int dm_insert_measurement(uint16_t device_id, const DataPayload *data) {
    if (!data || device_id == 0) return -1;

    pthread_mutex_lock(&g_db_mutex);

    sensor_sample_t s;
    memset(&s, 0, sizeof(s));
    s.device_id = device_id;
    s.temperature = data->temperature / 100.0;
    s.humidity = data->humi;
    s.run_status = data->status;
    s.output_count = data->production;
    s.sample_ts = data->timestamp;

    db_history_insert(&s);
    db_realtime_upsert(&s);
    alarm_check(&s);

    pthread_mutex_unlock(&g_db_mutex);
    return 0;
}

/* ========== 设备状态 ========== */

int dm_update_device_status(uint16_t device_id, uint8_t status) {
    if (device_id == 0) return -1;

    pthread_mutex_lock(&g_db_mutex);

    device_info_t got;
    if (db_device_get(device_id, &got) != DB_OK) {
        device_info_t dev;
        memset(&dev, 0, sizeof(dev));
        dev.device_id = device_id;
        snprintf(dev.name, sizeof(dev.name), "Device_%d", device_id);
        snprintf(dev.group_name, sizeof(dev.group_name), "默认分组");
        dev.registered_at = time(NULL);
        db_device_insert(&dev);
    }

    pthread_mutex_unlock(&g_db_mutex);
    return 0;
}

/* ========== HMI数据查询接口 ========== */

// 设备数量收集回调
static int dm_collect_device_count(const device_info_t *dev, void *ud) {
    (void)dev;
    int *count = (int *)ud;
    (*count)++;
    return 0;
}

// 设备ID收集回调
static int dm_collect_device_id(const device_info_t *dev, void *ud) {
    DeviceIdCollectCtx *ctx = (DeviceIdCollectCtx *)ud;
    if (ctx->count < ctx->capacity) {
        ctx->ids[ctx->count++] = dev->device_id;
    }
    return 0;
}

// 遍历所有设备信息
void dm_foreach_devices(dm_device_foreach_cb cb, void *userdata) {
    if (!cb) return;
    
    pthread_mutex_lock(&g_db_mutex);
    db_device_foreach((device_iter_cb)cb, userdata);
    pthread_mutex_unlock(&g_db_mutex);
}

// 遍历所有设备的实时数据
void dm_foreach_realtime(dm_realtime_foreach_cb cb, void *userdata) {
    if (!cb) return;
    
    pthread_mutex_lock(&g_db_mutex);
    
    // 用于收集设备ID的回调上下文
    DeviceIdCollectCtx ctx = { NULL, 0, 0 };
    
    // 先计算设备数量
    db_device_foreach(dm_collect_device_count, &ctx);
    
    // 分配设备ID数组
    if (ctx.count > 0) {
        ctx.ids = (uint16_t *)malloc(ctx.count * sizeof(uint16_t));
        if (!ctx.ids) {
            pthread_mutex_unlock(&g_db_mutex);
            return;
        }
        ctx.capacity = ctx.count;
        ctx.count = 0; // 重置计数器
        
        // 收集设备ID
        db_device_foreach(dm_collect_device_id, &ctx);
        
        // 查询每个设备的实时数据
        for (int i = 0; i < ctx.count; i++) {
            sensor_sample_t s;
            if (db_realtime_get(ctx.ids[i], &s) == DB_OK) {
                cb(&s, userdata);
            }
        }
        
        free(ctx.ids);
    }
    
    pthread_mutex_unlock(&g_db_mutex);
}

// 查询指定设备的历史数据
int dm_query_history(uint16_t device_id, int64_t from_sec, int64_t to_sec,
                     sensor_sample_t *out, int cap, int *count) {
    if (!out || !count || cap <= 0) return -1;
    
    pthread_mutex_lock(&g_db_mutex);
    int result = db_history_query(device_id, from_sec, to_sec, out, cap, count);
    pthread_mutex_unlock(&g_db_mutex);
    
    return (result == DB_OK) ? 0 : -1;
}

// 查询指定设备的实时数据
int dm_query_realtime(uint16_t device_id, sensor_sample_t *out) {
    if (!out) return -1;
    
    pthread_mutex_lock(&g_db_mutex);
    int result = db_realtime_get(device_id, out);
    pthread_mutex_unlock(&g_db_mutex);
    
    return (result == DB_OK) ? 0 : -1;
}
