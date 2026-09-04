#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <stdint.h>
#include "proto.h"
#include "storage.h"

/* ========== 数据库初始化与关闭 ========== */
int dm_init(const char *db_path);
void dm_close(void);

/* ========== 数据入库 ========== */
int dm_insert_measurement(uint16_t device_id, const DataPayload *data);

/* ========== 设备状态 ========== */
int dm_update_device_status(uint16_t device_id, uint8_t status);

/* ========== 告警引擎 ========== */
int dm_alarm_init(void);
int dm_alarm_check(uint16_t device_id, const DataPayload *data);
void dm_alarm_destroy(void);

/* ========== HMI数据查询接口 ========== */

// 查询回调函数类型
typedef void (*dm_device_foreach_cb)(const device_info_t *dev, void *userdata);
typedef void (*dm_realtime_foreach_cb)(const sensor_sample_t *sample, void *userdata);

// 遍历所有设备信息
void dm_foreach_devices(dm_device_foreach_cb cb, void *userdata);

// 遍历所有设备的实时数据
void dm_foreach_realtime(dm_realtime_foreach_cb cb, void *userdata);

// 查询指定设备的历史数据（毫秒时间戳）
int dm_query_history(uint16_t device_id, int64_t from_sec, int64_t to_sec,
                     sensor_sample_t *out, int cap, int *count);

// 查询指定设备的实时数据
int dm_query_realtime(uint16_t device_id, sensor_sample_t *out);

#endif
