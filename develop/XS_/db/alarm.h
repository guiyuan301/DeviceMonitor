/*
 * ============================================================================
 * alarm.h — 告警引擎接口（成员 D · 数据库与告警/测试工程师）
 * ============================================================================
 * 【定位】
 *  在 storage 之上提供"阈值规则判定 + 告警生成/恢复"能力。
 *  加载 alarm_rules 表 → 对每条采集数据判定 → 满足则写 alarm_records，
 *  不再满足则把对应告警置为"已恢复"。
 *
 * 【前置条件】
 *  使用本模块前须先调用 storage 的 db_open() 打开数据库；
 *  本模块内部通过 storage 的接口读写规则与告警记录。
 *
 * 【调用方式】
 *  成员 A 在入库线程中，每收到一个采样点：
 *    db_history_insert(&s);     // 写历史
 *    db_realtime_upsert(&s);    // 更新实时
 *    alarm_check(&s);           // 跑告警判定
 * ============================================================================
 */
#ifndef ALARM_H
#define ALARM_H

#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化告警引擎（加载全部启用规则）。返回 DB_OK / 错误码 */
int alarm_init(void);

/* 释放告警引擎内部资源 */
void alarm_destroy(void);

/* 重新从数据库加载规则（规则被 D 修改后调用） */
int alarm_reload_rules(void);

/* 对一条采样值做告警判定：
 *   - 命中规则且尚未告警 → 插入告警记录（status=0）
 *   - 未命中且之前告警中 → 置为已恢复（status=1, 写 recover_ts）
 * 返回 DB_OK；发生写库错误返回对应错误码 */
int alarm_check(const sensor_sample_t *s);

/* 查询某设备当前告警中（status=0）的告警条数 */
int alarm_open_count(int device_id, int *count);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_H */
