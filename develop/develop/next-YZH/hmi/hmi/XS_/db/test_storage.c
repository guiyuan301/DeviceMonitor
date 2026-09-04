/*
 * ============================================================================
 * test_storage.c — storage + alarm 接口自测（成员 D 测试脚本雏形）
 * 用法：gcc -o test test_storage.c storage.c ../alarm/alarm.c -lsqlite3
 * 覆盖：建表、设备增删改查、实时表 UPSERT、历史写入/回查/清理、规则增删、告警触发/恢复
 * ============================================================================
 * 本文件是 storage 层 + alarm 层的单元自测脚本：
 *   1. 依次调用 storage 的 db_* 接口，验证建表、设备、实时表、历史表的读写；
 *   2. 调用 alarm 接口验证规则加载、告警触发与恢复、重复触发去重；
 *   3. 所有断言通过 CHECK 宏统计，最后打印 PASS/FAIL 数量，任何失败返回非0退出码。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "storage.h"              /* storage 层数据库接口声明（db_* 系列） */
#include "alarm.h"                /* alarm 层告警引擎接口声明（alarm_* 系列） */

 /* 全局失败计数器：所有 CHECK 断言只要有一个失败就累加，最后决定进程退出码 */
static int g_fail = 0;

/* 断言宏：cond为真，则打印 [PASS]，否则打印 [FAIL] 并附带行号，同时 g_fail++ */
#define CHECK(cond, msg) do { \
    if (cond) { printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s (line %d)\n", msg, __LINE__); g_fail++; } \
} while (0)

int main(void)
{
    int rc, count;
    sensor_sample_t s, out;   /* s：待写入的采样数据；out：回查结果缓冲区 */

    /* 切换控制台代码页为 UTF-8，防止中文乱码（Windows 控制台默认 GBK） */
    system("chcp 65001 >nul");

    /* 1. 建库 */
    remove("xs.db");                  /* 先删掉旧的测试库文件，保证每次测试从干净环境开始 */
    rc = db_open("xs.db");            /* 打开/创建数据库 test.db */
    CHECK(rc == DB_OK, "db_open 建库");

    /* 2. devices 增删改查 */
    device_info_t dev = { 0 };            /* 设备结构体，先清零 */
    dev.device_id = 1;                  /* 设备编号 */
    strcpy(dev.name, "注塑机1号");        /* 设备名称 */
    strcpy(dev.group_name, "注塑车间");   /* 所属分组 */
    strcpy(dev.ip, "192.168.1.101");    /* 设备IP */
    dev.registered_at = time(NULL);     /* 注册时间 = 当前时间戳 */
    CHECK(db_device_insert(&dev) == DB_OK, "device 插入");   /* 插入设备 */

    device_info_t got;                                          /* 查询结果缓冲区 */
    CHECK(db_device_get(1, &got) == DB_OK, "device 查询");      /* 按id查询设备 */
    CHECK(strcmp(got.name, "注塑机1号") == 0, "device 名称正确"); /* 校验查询出的名称一致 */

    strcpy(dev.name, "注塑机1号A");                             /* 修改设备名称 */
    CHECK(db_device_update(&dev) == DB_OK, "device 更新");      /* 更新设备记录 */
    db_device_get(1, &got);                                     /* 重新查询验证更新 */
    CHECK(strcmp(got.name, "注塑机1号A") == 0, "device 更新生效"); /* 校验更新后的名称 */

    /* 3. 实时表 UPSERT */
    s.device_id = 1; s.temperature = 25.5; s.humidity = 55.0; s.run_status = 1;   /* 组装第一条实时数据 */
    s.output_count = 100; s.sample_ts = time(NULL);
    CHECK(db_realtime_upsert(&s) == DB_OK, "realtime upsert"); /* 首次写入实时表 */
    s.temperature = 26.0; s.humidity = 60.0; s.output_count = 103;                /* 同一设备第二次写，模拟覆盖 */
    CHECK(db_realtime_upsert(&s) == DB_OK, "realtime upsert 第二次");
    CHECK(db_realtime_get(1, &out) == DB_OK && out.temperature == 26.0
        && out.humidity == 60.0, "realtime 覆盖写生效(26.0/60.0)");  /* 校验 upsert 覆盖后拿到最新值 */

    /* 4. 历史表写入 + 回查 */
    sensor_sample_t h[10];      /* 历史回查结果缓冲区 */
    int64_t t0 = time(NULL);    /* 基准时间戳，模拟连续5个采样点 */
    for (int i = 0; i < 5; i++) {
        sensor_sample_t x = { 1, 25.0 + i, 50.0 + i, 1, 100 + i, t0 + i }; /* 温度逐点+1、湿度逐点+1 */
        CHECK(db_history_insert(&x) == DB_OK, "history 写入");           /* 写入一条历史采样 */
    }
    count = 0;
    CHECK(db_history_query(1, t0, t0 + 100, h, 10, &count) == DB_OK, "history 回查"); /* 按设备+时间范围回查 */
    CHECK(count == 5, "history 回查条数=5");                              /* 应查到5条 */
    CHECK(h[4].temperature == 29.0 && h[4].humidity == 54.0, "history 值正确(29.0/54.0)"); /* 校验第5条数值 */

    /* 5. 历史清理 */
    int del = 0;                                                /* 输出参数：实际删除条数 */
    CHECK(db_history_clean(t0 + 2, &del) == DB_OK, "history 清理"); /* 清理采样时间<=t0+2的历史 */
    CHECK(del == 2, "history 清理删除2条");                      /* 只删掉前2条（t0、t0+1） */

    /* 6. 告警规则 */
    alarm_rule_t rule;
    memset(&rule, 0, sizeof(rule));             /* 规则结构体清零 */
    rule.device_id = 0;                         /* 全局规则：device_id=0 表示对所有设备生效 */
    strcpy(rule.field, "temperature");          /* 监控字段：温度 */
    strcpy(rule.operator_, ">");                /* 比较运算符：大于 */
    rule.threshold = 30.0;                      /* 阈值：30度 */
    rule.level = 2;                             /* 告警级别 */
    strcpy(rule.description, "温度过高");        /* 规则描述 */
    rule.enabled = 1;                           /* 规则启用 */
    CHECK(db_rule_insert(&rule) == DB_OK && rule.id > 0, "rule 插入"); /* 插入规则，插入后回填自增id */
    CHECK(db_rule_foreach(1, NULL, NULL) == DB_ERR_PARAM, "rule foreach 参数校验"); /* 回调为空应报参数错误 */

    /* 7. 告警触发与恢复 */
    CHECK(alarm_init() == DB_OK, "alarm 初始化加载规则"); /* 初始化告警引擎，把规则加载进内存 */
    sensor_sample_t hot = { 1, 35.0, 70.0, 1, 103, t0 + 10 };   /* 35 > 30 → 触发 */  /* 构造高温采样点，触发告警 */
    CHECK(alarm_check(&hot) == DB_OK, "alarm 触发");          /* 检查告警条件，应触发 */
    CHECK(db_alarm_count_open(1, &count) == DB_OK && count == 1, "告警中=1"); /* 统计未恢复告警应=1 */
    alarm_record_t ar[10];                                    /* 告警记录查询缓冲区 */
    count = 0;
    CHECK(db_alarm_query_recent(1, ar, 10, &count) == DB_OK, "告警查询"); /* 查询最近告警记录 */
    CHECK(count == 1 && ar[0].status == 0, "告警记录 status=0");          /* 最新一条告警应为“告警中” */
    printf("   告警描述: %s\n", ar[0].description);            /* 打印自动生成的告警描述 */
    sensor_sample_t cool = { 1, 25.0, 45.0, 1, 103, t0 + 11 };  /* 25 < 30 → 恢复 */ /* 构造低温采样点，应恢复 */
    CHECK(alarm_check(&cool) == DB_OK, "alarm 恢复判定");      /* 温度回到阈值以下，告警应恢复 */
    CHECK(db_alarm_count_open(1, &count) == DB_OK && count == 0, "告警恢复后告警中=0"); /* 未恢复告警数应归0 */

    /* 8. 重复触发不刷屏 */
    CHECK(alarm_check(&hot) == DB_OK, "alarm 再触发");   /* 重新触发 */
    CHECK(alarm_check(&hot) == DB_OK, "alarm 重复触发(应去重)"); /* 同条件再次触发，应被内存去重 */
    CHECK(db_alarm_count_open(1, &count) == DB_OK && count == 1, "去重后告警中仍=1"); /* 未恢复告警数仍=1，证明没刷屏 */

    /* 9. 规则删除 */
    CHECK(db_rule_delete(rule.id) == DB_OK, "rule 删除"); /* 按id删除规则 */
    db_close();                                           /* 关闭数据库 */

    printf("\n==== %s: %d 失败 ====\n", g_fail ? "有失败" : "全部通过", g_fail); /* 汇总结果 */
    return g_fail ? 1 : 0;  /* 有失败返回1（非0），全部通过返回0 */
}
