/*
 * ============================================================================
 * storage.h — 数据库访问接口（成员 D · 数据库与告警/测试工程师）
 * ============================================================================
 * 【定位】
 *  中央板 SQLite3 数据库的统一访问层。封装 5 张表（devices / sensor_realtime /
 *  sensor_history / alarm_rules / alarm_records）的增删改查，以及事务、清理等
 *  维护操作。
 *
 * 【为什么是纯 C 接口】
 *  1) 成员 A 的中央板服务端是 Linux C，需直接调用本库；
 *  2) 成员 C 的 Qt 界面是 C++，天然兼容 C 接口，直接 #include 即可使用；
 *  3) 本模块不依赖任何框架（Qt/Boost 等），可在中央板与 PC 上独立编译。
 *
 * 【线程约定】
 *  SQLite3 同一连接默认不允许并发写。本库所有 db_* 函数为非线程安全设计，
 *  调用方须保证串行：建议所有写操作集中在"入库线程"（单写者），界面线程只读
 *  或通过队列把请求交给入库线程，禁止多线程直接同时调用写接口。
 *
 * 【使用示例（成员 A 调用）】
 *  #include "storage.h"
 *  int rc = db_open("/data/monitor.db");      // 打开并自动建表
 *  sensor_sample_t s = {1, 25.5, 60.0, 1, 1234, 1724745600};
 *  rc = db_history_insert(&s);                // 写历史表
 *  rc = db_realtime_upsert(&s);               // 覆盖实时表
 *  db_close();
 * ============================================================================
 */
#ifndef STORAGE_H
#define STORAGE_H

#include <sqlite3.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * 错误码约定
 * -------------------------------------------------------------------------- */
#define DB_OK             0    /* 成功 */
#define DB_ERR_OPEN      -1    /* 打开/创建数据库失败 */
#define DB_ERR_SQL       -2    /* SQL 执行失败 */
#define DB_ERR_PARAM     -3    /* 参数非法（空指针/非法值） */
#define DB_ERR_NOT_FOUND -4    /* 记录不存在 */
#define DB_ERR_BUSY      -5    /* 数据库忙（锁冲突） */

/* ----------------------------------------------------------------------------
 * 数据结构（对应数据库表）
 * -------------------------------------------------------------------------- */

/* devices 表 —— 设备信息 */
typedef struct {
    int        device_id;                  /* 协议设备号 1~65535（主键） */
    char       name[64];                   /* 设备名称 */
    char       group_name[64];             /* 分组 */
    char       ip[32];                     /* 采集板 IP */
    int64_t    registered_at;              /* 注册时间 Unix 秒 */
} device_info_t;

/* sensor_realtime / sensor_history —— 采样值 */
typedef struct {
    int        device_id;                  /* 设备号 */
    double     temperature;                /* 温度 ℃（实际值） */
    double     humidity;                   /* 湿度 %RH（实际值） */
    int        run_status;                 /* 运行状态 0=停机 1=运行 */
    int64_t    output_count;               /* 产量累计值（里程表式读数） */
    int64_t    sample_ts;                  /* 采样时间 Unix 秒 */
} sensor_sample_t;

/* alarm_rules 表 —— 告警规则 */
typedef struct {
    int        id;                         /* 规则 id（0 表示由数据库生成） */
    int        device_id;                  /* 0=全局规则；>0=指定设备 */
    char       field[32];                  /* temperature / humidity / run_status / output_count */
    char       operator_[8];               /* > >= < <= = != */
    double     threshold;                  /* 阈值（温度用实际℃） */
    int        level;                      /* 级别 1低 2中 3高 */
    char       description[128];           /* 规则描述 */
    int        enabled;                    /* 1启用 0停用 */
    int64_t    created_at;                 /* 创建时间 */
} alarm_rule_t;

/* alarm_records 表 —— 告警记录 */
typedef struct {
    int        id;                         /* 记录 id（0 表示由数据库生成） */
    int        device_id;                  /* 告警设备号 */
    int        rule_id;                    /* 命中的规则 id */
    int        level;                      /* 级别（冗余） */
    char       description[128];           /* 告警描述 */
    int64_t    trigger_ts;                 /* 触发时间 Unix 秒 */
    int64_t    recover_ts;                 /* 恢复时间，0 表示未恢复 */
    int        status;                     /* 0=告警中 1=已恢复 */
} alarm_record_t;

/* 设备遍历回调 */
typedef int (*device_iter_cb)(const device_info_t *dev, void *userdata);
/* 规则遍历回调 */
typedef int (*rule_iter_cb)(const alarm_rule_t *rule, void *userdata);

/* ----------------------------------------------------------------------------
 * 一、数据库生命周期
 * -------------------------------------------------------------------------- */

/* 打开（或创建）数据库并建表。db_path 为空时使用默认路径 ":memory:"。
 * 成功返回 DB_OK；失败返回 DB_ERR_OPEN。 */
int db_open(const char *db_path);

/* 关闭数据库连接。 */
void db_close(void);

/* 返回底层 sqlite3 句柄（仅用于非常规操作，一般用不到）。 */
sqlite3 *db_handle(void);

/* 开启/提交/回滚事务。批量写时用：db_begin() → N 次 insert → db_commit()。 */
int db_begin(void);
int db_commit(void);
int db_rollback(void);

/* ----------------------------------------------------------------------------
 * 二、devices 设备表（增删改查）
 * -------------------------------------------------------------------------- */

/* 新增一台设备（device_id 重复返回 DB_ERR_SQL） */
int db_device_insert(const device_info_t *dev);

/* 按 device_id 更新设备信息（name/group_name/ip 等） */
int db_device_update(const device_info_t *dev);

/* 按 device_id 删除设备 */
int db_device_delete(int device_id);

/* 按 device_id 查询单台设备，结果写入 out；不存在返回 DB_ERR_NOT_FOUND */
int db_device_get(int device_id, device_info_t *out);

/* 遍历全部设备，每台回调一次 cb；cb 返回非 0 则提前终止 */
int db_device_foreach(device_iter_cb cb, void *userdata);

/* ----------------------------------------------------------------------------
 * 三、sensor_realtime 实时值表（每设备最新一条）
 * -------------------------------------------------------------------------- */

/* 覆盖写最新值（UPSERT：有则更新、无则插入）。界面看板秒刷只读本表 */
int db_realtime_upsert(const sensor_sample_t *s);

/* 查询某设备最新值；不存在返回 DB_ERR_NOT_FOUND */
int db_realtime_get(int device_id, sensor_sample_t *out);

/* ----------------------------------------------------------------------------
 * 四、sensor_history 历史表（每个采样点一条）
 * -------------------------------------------------------------------------- */

/* 写入一个采样点（历史表，数据源） */
int db_history_insert(const sensor_sample_t *s);

/* 按设备 + 时间区间查询历史。out 为调用方预分配数组（容量 cap），
 * 返回实际条数到 *count。区间为 [begin_ts, end_ts]。 */
int db_history_query(int device_id, int64_t begin_ts, int64_t end_ts,
                     sensor_sample_t *out, int cap, int *count);

/* 删除采样时间早于 before_ts 的旧数据（防膨胀），返回删除行数到 *deleted */
int db_history_clean(int64_t before_ts, int *deleted);

/* ----------------------------------------------------------------------------
 * 五、alarm_rules 告警规则表
 * -------------------------------------------------------------------------- */

/* 新增一条规则；生成的自增 id 写回 rule->id（若 rule->id==0） */
int db_rule_insert(alarm_rule_t *rule);

/* 按 id 更新规则 */
int db_rule_update(const alarm_rule_t *rule);

/* 按 id 删除规则 */
int db_rule_delete(int id);

/* 遍历规则（可按 device_id 过滤：filter<=0 表示全部；>0 表示该设备+全局） */
int db_rule_foreach(int filter_device, rule_iter_cb cb, void *userdata);

/* ----------------------------------------------------------------------------
 * 六、alarm_records 告警记录表
 * -------------------------------------------------------------------------- */

/* 插入一条告警记录；生成的自增 id 写回 rec->id（若 rec->id==0） */
int db_alarm_insert(alarm_record_t *rec);

/* 更新告警状态：status(0/1) + 恢复时间（status=1 时写 recover_ts） */
int db_alarm_update_status(int id, int status, int64_t recover_ts);

/* 查询某设备最新 N 条告警（desc 排序）；同样用预分配数组 */
int db_alarm_query_recent(int device_id, alarm_record_t *out, int cap, int *count);

/* 查询某设备未恢复（status=0）的告警条数 */
int db_alarm_count_open(int device_id, int *count);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
