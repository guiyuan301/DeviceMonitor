#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <stdint.h>
#include "proto.h"  // 引入协议结构体

/* ========== 数据库初始化与关闭 ========== */
// 初始化数据库（创建表结构、打开连接）
// db_path: 数据库文件路径（例如 "production_data.db"）
// 返回值: 0成功, -1失败
int db_init(const char *db_path);

// 关闭数据库连接并释放资源
void db_close(void);

/* ========== 数据入库接口 ========== */
// 将解析后的设备数据写入数据库（线程安全）
// device_id: 设备号
// data: 解析后的负载数据 (来自 parse_packet 或回调)
// 返回值: 0成功, -1失败
int db_insert_measurement(uint16_t device_id, const DataPayload *data);

/* ========== 设备状态更新接口 ========== */
// 更新设备在线状态 (1=在线, 0=离线)
// 返回值: 0成功, -1失败
int db_update_device_status(uint16_t device_id, uint8_t status);

#endif