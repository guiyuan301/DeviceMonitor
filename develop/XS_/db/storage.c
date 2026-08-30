/*
 * ============================================================================
 * storage.c — 数据库访问接口实现（SQLite3 C API）
 * ============================================================================
 * 编译：gcc -c storage.c -lsqlite3        （中央板/PC 均适用）
 * 说明：所有 SQL 一律使用参数绑定（sqlite3_bind_*）防止注入与转义错误。
 *       连接打开时自动执行 PRAGMA（WAL）并建表，见 db_schema 常量。
 * ============================================================================
 */
#include "storage.h"

#include <string.h>
#include <stdio.h>

 /* 全局连接句柄（单连接，调用方保证串行） */
static sqlite3* g_db = NULL;

/* ============================================================================
 * 建表 SQL —— 数据库首次打开时自动执行
 * 包含：PRAGMA 优化 + 5 张业务表 + 3 个索引
 * ============================================================================
 */
static const char* db_schema =
/* --- WAL 模式：允许读写并发，提高写密集场景性能 --- */
"PRAGMA journal_mode = WAL;"
/* --- NORMAL 同步：WAL 模式下平衡安全与性能 --- */
"PRAGMA synchronous = NORMAL;"
/* --- 启用外键约束（SQLite 默认关闭） --- */
"PRAGMA foreign_keys = ON;"

/* --- devices 设备表：存储设备基本信息（注册时写入） --- */
"CREATE TABLE IF NOT EXISTS devices ("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"   /* 内部自增 id */
"  device_id INTEGER NOT NULL UNIQUE,"       /* 协议设备号（业务主键） */
"  name TEXT NOT NULL DEFAULT '',"           /* 设备名称 */
"  group_name TEXT NOT NULL DEFAULT '默认分组'," /* 分组名 */
"  ip TEXT NOT NULL DEFAULT '',"             /* 采集板 IP */
"  registered_at INTEGER NOT NULL DEFAULT 0);" /* 注册时间（Unix 秒） */

/* --- sensor_realtime 实时值表：每设备仅保留最新一条（用于看板秒刷） --- */
"CREATE TABLE IF NOT EXISTS sensor_realtime ("
"  device_id INTEGER PRIMARY KEY,"            /* 设备号（主键，每设备一行） */
"  temperature REAL NOT NULL DEFAULT 0,"      /* 温度 ℃ */
"  humidity REAL NOT NULL DEFAULT 0,"         /* 湿度 %RH */
"  run_status INTEGER NOT NULL DEFAULT 0,"    /* 0=停机 1=运行 */
"  output_count INTEGER NOT NULL DEFAULT 0,"  /* 产量累计（里程表读数） */
"  sample_ts INTEGER NOT NULL DEFAULT 0,"     /* 采样时间 */
"  updated_at INTEGER NOT NULL DEFAULT 0);"   /* 最后更新时间 */

/* --- sensor_history 历史表：每个采样点一条（数据源，定时清理旧数据） --- */
"CREATE TABLE IF NOT EXISTS sensor_history ("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  device_id INTEGER NOT NULL,"
"  temperature REAL NOT NULL DEFAULT 0,"
"  humidity REAL NOT NULL DEFAULT 0,"
"  run_status INTEGER NOT NULL DEFAULT 0,"
"  output_count INTEGER NOT NULL DEFAULT 0,"
"  sample_ts INTEGER NOT NULL DEFAULT 0);"
/* 历史查询高频走 device_id + sample_ts，建联合索引 */
"CREATE INDEX IF NOT EXISTS idx_history_device_ts"
"  ON sensor_history(device_id, sample_ts);"

/* --- alarm_rules 告警规则表：支持全局规则和指定设备规则 --- */
"CREATE TABLE IF NOT EXISTS alarm_rules ("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  device_id INTEGER,"                        /* NULL=全局规则 */
"  field TEXT NOT NULL,"                      /* 监控字段：temperature/run_status/output_count */
"  operator TEXT NOT NULL,"                   /* 比较运算符：> >= < <= = != */
"  threshold REAL NOT NULL,"                  /* 阈值 */
"  level INTEGER NOT NULL DEFAULT 1,"         /* 告警级别 1低 2中 3高 */
"  description TEXT NOT NULL DEFAULT '',"
"  enabled INTEGER NOT NULL DEFAULT 1,"       /* 1=启用 0=停用 */
"  created_at INTEGER NOT NULL DEFAULT 0);"

/* --- alarm_records 告警记录表：触发时插入，恢复时更新 recover_ts --- */
"CREATE TABLE IF NOT EXISTS alarm_records ("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  device_id INTEGER NOT NULL,"
"  rule_id INTEGER,"                          /* 命中的规则 id */
"  level INTEGER NOT NULL DEFAULT 1,"
"  description TEXT NOT NULL DEFAULT '',"
"  trigger_ts INTEGER NOT NULL,"              /* 触发时间 */
"  recover_ts INTEGER,"                       /* 恢复时间，NULL=未恢复 */
"  status INTEGER NOT NULL DEFAULT 0);"       /* 0=告警中 1=已恢复 */
"CREATE INDEX IF NOT EXISTS idx_alarm_device_ts"
"  ON alarm_records(device_id, trigger_ts);"
"CREATE INDEX IF NOT EXISTS idx_alarm_status"
"  ON alarm_records(status);";

/* ============================================================================
 * 一、数据库生命周期
 *    打开 → 建表 → 业务操作 → 关闭
 * ============================================================================ */

 /*
  * db_open — 打开或创建数据库文件
  * @db_path: 数据库文件路径，传 NULL 则使用内存数据库 ":memory:"
  * 返回: DB_OK 成功 / DB_ERR_OPEN 打开失败 / DB_ERR_SQL 建表失败
  *
  * 流程：
  *  1. 如果已有连接则先关闭（防止泄漏）
  *  2. sqlite3_open 打开文件（不存在会自动创建）
  *  3. 执行 db_schema 完成 PRAGMA 配置和建表
  */
int db_open(const char* db_path)
{
    int rc;
    char* err = NULL;

    /* 有旧连接先关闭，避免泄漏 */
    if (g_db)
        db_close();

    /* 打开数据库文件；db_path 为 NULL 时创建临时内存数据库 */
    rc = sqlite3_open(db_path ? db_path : ":memory:", &g_db);
    if (rc != SQLITE_OK) {
        /* 打开失败时 g_db 可能非 NULL，必须关闭并置空 */
        if (g_db) {
            sqlite3_close(g_db);
            g_db = NULL;
        }
        return DB_ERR_OPEN;
    }

    /* 一次性执行建表 + PRAGMA 配置 */
    rc = sqlite3_exec(g_db, db_schema, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);   /* sqlite3_exec 的错误信息必须用 sqlite3_free 释放 */
        sqlite3_close(g_db);
        g_db = NULL;
        return DB_ERR_SQL;
    }
    return DB_OK;
}

/*
 * db_close — 关闭数据库连接
 * 无返回值。调用后 g_db 置空，后续操作需重新 db_open。
 */
void db_close(void)
{
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

/*
 * db_handle — 获取底层 sqlite3 句柄
 * 仅用于非常规操作（如 PRAGMA 设置、备份等），一般业务代码不要直接用。
 */
sqlite3* db_handle(void)
{
    return g_db;
}

/*
 * db_begin — 开启事务（IMMEDIATE 模式）
 * IMMEDIATE：立即获取写锁，适合写多读少场景。
 * 如果数据库已被其他写事务占用，返回 DB_ERR_BUSY。
 */
int db_begin(void)
{
    char* err = NULL;
    int rc = sqlite3_exec(g_db, "BEGIN IMMEDIATE;", NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
        return (rc == SQLITE_BUSY) ? DB_ERR_BUSY : DB_ERR_SQL;
    }
    return DB_OK;
}

/*
 * db_commit — 提交事务
 * 配合 db_begin 使用，批量写入时：db_begin → N 次 insert → db_commit
 */
int db_commit(void)
{
    char* err = NULL;
    int rc = sqlite3_exec(g_db, "COMMIT;", NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
        return (rc == SQLITE_BUSY) ? DB_ERR_BUSY : DB_ERR_SQL;
    }
    return DB_OK;
}

/*
 * db_rollback — 回滚事务
 * 出错时调用，撤销 db_begin 之后的所有写操作。
 */
int db_rollback(void)
{
    char* err = NULL;
    int rc = sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
        return DB_ERR_SQL;
    }
    return DB_OK;
}

/* ============================================================================
 * 二、devices 设备表 —— CRUD
 * ============================================================================ */

 /*
  * db_device_insert — 新增设备
  * @dev: 设备信息（device_id 必须 > 0）
  * 注意：device_id 有 UNIQUE 约束，重复插入会返回 DB_ERR_SQL（ SQLITE_CONSTRAINT ）。
  *
  * SQLite3 绑定参数说明：
  *  ?1 ~ ?5 对应 SQL 中的 5 个占位符，按顺序绑定即可。
  *  sqlite3_bind_text 的最后一个参数 SQLITE_STATIC 表示调用方保证字符串
  *  在 step 前不会被释放（这里 dev 在函数生命周期内有效）。
  */
int db_device_insert(const device_info_t* dev)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!dev || dev->device_id <= 0)
        return DB_ERR_PARAM;

    /* prepare：编译 SQL 为字节码，返回 stmt 句柄 */
    rc = sqlite3_prepare_v2(g_db,
        "INSERT INTO devices(device_id,name,group_name,ip,registered_at)"
        " VALUES(?1,?2,?3,?4,?5);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    /* 按位置绑定参数（从 1 开始） */
    sqlite3_bind_int(stmt, 1, dev->device_id);
    sqlite3_bind_text(stmt, 2, dev->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, dev->group_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, dev->ip, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, dev->registered_at);

    /* step：执行已绑定的语句。INSERT 成功返回 SQLITE_DONE */
    rc = sqlite3_step(stmt);
    /* finalize：释放 stmt 资源（必须调用，否则内存泄漏） */
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_device_update — 更新设备信息
 * 按 device_id 匹配，更新 name / group_name / ip / registered_at。
 * 如果 device_id 不存在，SQL 正常执行但影响 0 行（仍返回 DB_OK）。
 */
int db_device_update(const device_info_t* dev)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!dev || dev->device_id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "UPDATE devices SET name=?1, group_name=?2, ip=?3, registered_at=?4"
        " WHERE device_id=?5;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_text(stmt, 1, dev->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, dev->group_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, dev->ip, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, dev->registered_at);
    sqlite3_bind_int(stmt, 5, dev->device_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_device_delete — 删除设备
 * 注意：只删 devices 表，不联动删除 realtime / history / alarm 数据。
 * 如需级联删除，调用方应先清理关联表。
 */
int db_device_delete(int device_id)
{
    sqlite3_stmt* stmt;
    int rc;

    if (device_id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db, "DELETE FROM devices WHERE device_id=?1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, device_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_device_get — 查询单台设备
 * 将查询结果写入 out 结构体。不存在则返回 DB_ERR_NOT_FOUND。
 *
 * sqlite3_column_text 返回 NULL 时，snprintf 会将其格式化为 "(null)"，
 * 这里做了简单的空指针保护。
 */
int db_device_get(int device_id, device_info_t* out)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!out || device_id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "SELECT device_id,name,group_name,ip,registered_at"
        " FROM devices WHERE device_id=?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, device_id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        /* 逐列提取数据，sqlite3_column_* 的第二个参数是列索引（从 0 开始） */
        out->device_id = sqlite3_column_int(stmt, 0);
        snprintf(out->name, sizeof(out->name), "%s",
            (const char*)sqlite3_column_text(stmt, 1));
        snprintf(out->group_name, sizeof(out->group_name), "%s",
            (const char*)sqlite3_column_text(stmt, 2));
        snprintf(out->ip, sizeof(out->ip), "%s",
            (const char*)sqlite3_column_text(stmt, 3));
        out->registered_at = sqlite3_column_int64(stmt, 4);
        sqlite3_finalize(stmt);
        return DB_OK;
    }
    sqlite3_finalize(stmt);
    return DB_ERR_NOT_FOUND;
}

/*
 * db_device_foreach — 遍历全部设备
 * @cb:       回调函数，每台设备调用一次
 * @userdata: 透传给回调的用户数据
 * cb 返回 0 继续遍历，非 0 立即终止（用于提前退出）。
 *
 * SQL 按 device_id 升序排列，保证遍历顺序稳定。
 */
int db_device_foreach(device_iter_cb cb, void* userdata)
{
    sqlite3_stmt* stmt;
    device_info_t dev;
    int rc;

    if (!cb)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "SELECT device_id,name,group_name,ip,registered_at"
        " FROM devices ORDER BY device_id;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    /* 循环 step，每次 sqlite3_step 返回 SQLITE_ROW 表示还有数据 */
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        dev.device_id = sqlite3_column_int(stmt, 0);
        snprintf(dev.name, sizeof(dev.name), "%s",
            (const char*)sqlite3_column_text(stmt, 1));
        snprintf(dev.group_name, sizeof(dev.group_name), "%s",
            (const char*)sqlite3_column_text(stmt, 2));
        snprintf(dev.ip, sizeof(dev.ip), "%s",
            (const char*)sqlite3_column_text(stmt, 3));
        dev.registered_at = sqlite3_column_int64(stmt, 4);
        if (cb(&dev, userdata) != 0)
            break;
    }
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE || rc == SQLITE_ROW) ? DB_OK : DB_ERR_SQL;
}

/* ============================================================================
 * 三、sensor_realtime 实时值表
 *    每设备仅一行，用 UPSERT（INSERT OR REPLACE 语义）保证最新值。
 *    看板界面秒刷只读本表，不需要扫 history。
 * ============================================================================ */

 /*
  * db_realtime_upsert — 覆盖写最新值（有则更新，无则插入）
  * @s: 采样数据
  *
  * 核心 SQL：INSERT ... ON CONFLICT(device_id) DO UPDATE SET ...
  * 这是 SQLite3 的 UPSERT 语法（3.24.0+），比 REPLACE 更安全：
  *  - REPLACE 实际是 DELETE + INSERT，会触发外键 ON DELETE
  *  - UPSERT 只更新冲突行，性能更好
  *
  * updated_at 直接复用 sample_ts（采样时间即更新时间）。
  */
int db_realtime_upsert(const sensor_sample_t* s)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!s || s->device_id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "INSERT INTO sensor_realtime"
        "(device_id,temperature,humidity,run_status,output_count,sample_ts,updated_at)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7)"
        " ON CONFLICT(device_id) DO UPDATE SET"
        " temperature=excluded.temperature, humidity=excluded.humidity,"
        " run_status=excluded.run_status, output_count=excluded.output_count,"
        " sample_ts=excluded.sample_ts, updated_at=excluded.updated_at;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, s->device_id);
    sqlite3_bind_double(stmt, 2, s->temperature);
    sqlite3_bind_double(stmt, 3, s->humidity);
    sqlite3_bind_int(stmt, 4, s->run_status);
    sqlite3_bind_int64(stmt, 5, s->output_count);
    sqlite3_bind_int64(stmt, 6, s->sample_ts);
    sqlite3_bind_int64(stmt, 7, s->sample_ts);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_realtime_get — 查询某设备的最新实时值
 * 不存在返回 DB_ERR_NOT_FOUND。界面看板用此接口获取单设备数据。
 */
int db_realtime_get(int device_id, sensor_sample_t* out)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!out || device_id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "SELECT device_id,temperature,humidity,run_status,output_count,sample_ts"
        " FROM sensor_realtime WHERE device_id=?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, device_id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        out->device_id = sqlite3_column_int(stmt, 0);
        out->temperature = sqlite3_column_double(stmt, 1);
        out->humidity = sqlite3_column_double(stmt, 2);
        out->run_status = sqlite3_column_int(stmt, 3);
        out->output_count = sqlite3_column_int64(stmt, 4);
        out->sample_ts = sqlite3_column_int64(stmt, 5);
        sqlite3_finalize(stmt);
        return DB_OK;
    }
    sqlite3_finalize(stmt);
    return DB_ERR_NOT_FOUND;
}

/* ============================================================================
 * 四、sensor_history 历史表
 *    每个采样点写一条，支持按时间区间查询和过期清理。
 *    是数据的"源头"；realtime 表只是从这里同步的最新快照。
 * ============================================================================ */

 /*
  * db_history_insert — 写入一个历史采样点
  * 调用方每采集一次就调用一次，典型频率 1~5 秒/次。
  */
int db_history_insert(const sensor_sample_t* s)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!s || s->device_id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "INSERT INTO sensor_history"
        "(device_id,temperature,humidity,run_status,output_count,sample_ts)"
        " VALUES(?1,?2,?3,?4,?5,?6);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, s->device_id);
    sqlite3_bind_double(stmt, 2, s->temperature);
    sqlite3_bind_double(stmt, 3, s->humidity);
    sqlite3_bind_int(stmt, 4, s->run_status);
    sqlite3_bind_int64(stmt, 5, s->output_count);
    sqlite3_bind_int64(stmt, 6, s->sample_ts);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_history_query — 按设备 + 时间区间查询历史
 * @device_id:  设备号
 * @begin_ts:   起始时间（包含）
 * @end_ts:     结束时间（包含）
 * @out:        调用方提前分配好的数组
 * @cap:        数组容量（最多返回 cap 条）
 * @count:      [输出] 实际返回的条数
 *
 * SQL 用 BETWEEN ?2 AND ?3 做区间过滤，ORDER BY sample_ts ASC 升序。
 * 调用方只需分配 sensor_sample_t 数组，不需要释放。
 *
 * 返回 DB_OK（即使 count < cap 也是正常——区间内数据不足）。
 */
int db_history_query(int device_id, int64_t begin_ts, int64_t end_ts,
    sensor_sample_t* out, int cap, int* count)
{
    sqlite3_stmt* stmt;
    int rc, n = 0;

    if (!out || !count || cap <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "SELECT device_id,temperature,humidity,run_status,output_count,sample_ts"
        " FROM sensor_history"
        " WHERE device_id=?1 AND sample_ts BETWEEN ?2 AND ?3"
        " ORDER BY sample_ts ASC;", -1, &stmt, NULL);// 按时间从小到大排序
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, device_id);
    sqlite3_bind_int64(stmt, 2, begin_ts);
    sqlite3_bind_int64(stmt, 3, end_ts);

    /* 逐行读取，直到数组满或没有更多数据 */
    while (n < cap && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        out[n].device_id = sqlite3_column_int(stmt, 0);
        out[n].temperature = sqlite3_column_double(stmt, 1);
        out[n].humidity = sqlite3_column_double(stmt, 2);
        out[n].run_status = sqlite3_column_int(stmt, 3);
        out[n].output_count = sqlite3_column_int64(stmt, 4);
        out[n].sample_ts = sqlite3_column_int64(stmt, 5);
        n++;
    }
    sqlite3_finalize(stmt);
    *count = n;
    return (rc == SQLITE_DONE || rc == SQLITE_ROW) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_history_clean — 清理过期历史数据
 * @before_ts:  删除 sample_ts < before_ts 的所有记录
 * @deleted:    [输出] 实际删除的行数（可为 NULL）
 *
 * 定时任务调用：每天凌晨删除 30 天前的数据，防止表无限膨胀。
 * sqlite3_changes() 返回上一次执行影响的行数。
 */
int db_history_clean(int64_t before_ts, int* deleted)
{
    sqlite3_stmt* stmt;
    int rc;

    rc = sqlite3_prepare_v2(g_db,
        "DELETE FROM sensor_history WHERE sample_ts < ?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int64(stmt, 1, before_ts);
    rc = sqlite3_step(stmt);
    if (deleted)
        //SQLite 库函数，返回上一条SQL语句影响的行数，这里就是本次DELETE真正删掉了多少条记录。
        //deleted传NULL，这里就不赋值
        *deleted = sqlite3_changes(g_db);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/* ============================================================================
 * 五、alarm_rules 告警规则表
 *    规则有两种作用域：
 *    - device_id = NULL（存为 NULL）→ 全局规则，对所有设备生效
 *    - device_id > 0                → 指定设备规则
 * ============================================================================ */

 /*
  * db_rule_insert — 新增告警规则
  * @rule: 规则信息。若 rule->id == 0，插入后自动回填自增 id。
  *
  * device_id 为 0 时绑定 NULL（对应数据库 NULL），表示全局规则。
  * sqlite3_last_insert_rowid() 获取刚插入行的 ROWID（即自增 id）。
  */
int db_rule_insert(alarm_rule_t* rule)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!rule || !rule->field[0] || !rule->operator_[0])
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "INSERT INTO alarm_rules"
        "(device_id,field,operator,threshold,level,description,enabled,created_at)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    /* device_id > 0 绑定具体值，否则绑定 NULL（全局规则） */
    if (rule->device_id > 0)
        sqlite3_bind_int(stmt, 1, rule->device_id);
    else
        sqlite3_bind_null(stmt, 1);
    sqlite3_bind_text(stmt, 2, rule->field, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, rule->operator_, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 4, rule->threshold);
    sqlite3_bind_int(stmt, 5, rule->level);
    sqlite3_bind_text(stmt, 6, rule->description, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, rule->enabled);
    sqlite3_bind_int64(stmt, 8, rule->created_at);

    rc = sqlite3_step(stmt);
    /* 插入成功且调用方未指定 id 时，回填数据库生成的自增 id */
    if (rc == SQLITE_DONE && rule->id == 0)
        rule->id = (int)sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_rule_update — 按 id 更新规则
 * 可修改字段值、阈值、级别、描述、启用状态等。
 * 注意：不能修改 device_id 以外的主键字段。
 */
int db_rule_update(const alarm_rule_t* rule)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!rule || rule->id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "UPDATE alarm_rules SET device_id=?1, field=?2, operator=?3,"
        " threshold=?4, level=?5, description=?6, enabled=?7"
        " WHERE id=?8;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    if (rule->device_id > 0)
        sqlite3_bind_int(stmt, 1, rule->device_id);
    else
        sqlite3_bind_null(stmt, 1);
    sqlite3_bind_text(stmt, 2, rule->field, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, rule->operator_, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 4, rule->threshold);
    sqlite3_bind_int(stmt, 5, rule->level);
    sqlite3_bind_text(stmt, 6, rule->description, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, rule->enabled);
    sqlite3_bind_int(stmt, 8, rule->id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_rule_delete — 按 id 删除规则
 */
int db_rule_delete(int id)
{
    sqlite3_stmt* stmt;
    int rc;

    if (id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db, "DELETE FROM alarm_rules WHERE id=?1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_rule_foreach — 遍历告警规则（可按设备过滤）
 * @filter_device: >0 时只返回该设备的规则 + 全局规则（device_id IS NULL）
 *                 <=0 时返回所有已启用规则
 * @cb: 回调函数，返回非 0 提前终止
 *
 * WHERE 条件中 enabled=1 表示只遍历启用的规则。
 * device_id IS NULL 的判断用 sqlite3_column_type 检测 NULL。
 */
int db_rule_foreach(int filter_device, rule_iter_cb cb, void* userdata)
{
    sqlite3_stmt* stmt;
    alarm_rule_t rule;
    int rc;

    if (!cb)
        return DB_ERR_PARAM;

    /* 按设备过滤时需绑定参数，否则不需要 */
    if (filter_device > 0)
        rc = sqlite3_prepare_v2(g_db,
            "SELECT id,device_id,field,operator,threshold,level,description,enabled,created_at"
            " FROM alarm_rules WHERE enabled=1 AND (device_id IS NULL OR device_id=?1)"
            " ORDER BY id;", -1, &stmt, NULL);
    else
        rc = sqlite3_prepare_v2(g_db,
            "SELECT id,device_id,field,operator,threshold,level,description,enabled,created_at"
            " FROM alarm_rules WHERE enabled=1 ORDER BY id;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    if (filter_device > 0)
        sqlite3_bind_int(stmt, 1, filter_device);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        rule.id = sqlite3_column_int(stmt, 0);
        /* 检测 NULL 值：device_id 为 NULL 时置 0（全局规则） */
        rule.device_id = sqlite3_column_type(stmt, 1) == SQLITE_NULL
            ? 0 : sqlite3_column_int(stmt, 1);
        snprintf(rule.field, sizeof(rule.field), "%s",
            (const char*)sqlite3_column_text(stmt, 2));
        snprintf(rule.operator_, sizeof(rule.operator_), "%s",
            (const char*)sqlite3_column_text(stmt, 3));
        rule.threshold = sqlite3_column_double(stmt, 4);
        rule.level = sqlite3_column_int(stmt, 5);
        snprintf(rule.description, sizeof(rule.description), "%s",
            (const char*)sqlite3_column_text(stmt, 6));
        rule.enabled = sqlite3_column_int(stmt, 7);
        rule.created_at = sqlite3_column_int64(stmt, 8);
        if (cb(&rule, userdata) != 0)
            break;
    }
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE || rc == SQLITE_ROW) ? DB_OK : DB_ERR_SQL;
}

/* ============================================================================
 * 六、alarm_records 告警记录表
 *    告警触发时插入一条记录（status=0），恢复时更新 recover_ts + status=1。
 * ============================================================================ */

 /*
  * db_alarm_insert — 插入一条告警记录
  * @rec: 告警记录。rule_id 和 recover_ts 可为 NULL（未关联规则 / 未恢复）。
  * 插入成功后自动回填自增 id。
  */
int db_alarm_insert(alarm_record_t* rec)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!rec || rec->device_id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "INSERT INTO alarm_records"
        "(device_id,rule_id,level,description,trigger_ts,recover_ts,status)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, rec->device_id);
    /* rule_id 可能为 0（未关联规则），绑定 NULL */
    if (rec->rule_id > 0)
        sqlite3_bind_int(stmt, 2, rec->rule_id);
    else
        sqlite3_bind_null(stmt, 2);
    sqlite3_bind_int(stmt, 3, rec->level);
    sqlite3_bind_text(stmt, 4, rec->description, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, rec->trigger_ts);
    /* recover_ts 为 0 表示未恢复，绑定 NULL */
    if (rec->recover_ts > 0)
        sqlite3_bind_int64(stmt, 6, rec->recover_ts);
    else
        sqlite3_bind_null(stmt, 6);
    sqlite3_bind_int(stmt, 7, rec->status);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE && rec->id == 0)
        rec->id = (int)sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_alarm_update_status — 更新告警状态（恢复时调用）
 * @id:          记录 id
 * @status:      0=告警中 / 1=已恢复
 * @recover_ts:  恢复时间（status=1 时有意义，status=0 时传 0 即可）
 *
 * 典型流程：告警触发 → db_alarm_insert (status=0) → 恢复 → db_alarm_update_status(1, ts)
 */
int db_alarm_update_status(int id, int status, int64_t recover_ts)
{
    sqlite3_stmt* stmt;
    int rc;

    if (id <= 0)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "UPDATE alarm_records SET status=?1, recover_ts=?2 WHERE id=?3;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_int64(stmt, 2, recover_ts);
    sqlite3_bind_int(stmt, 3, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_alarm_query_recent — 查询最近 N 条告警记录
 * @device_id: >0 指定设备，<=0 查全部
 * @out:       预分配数组
 * @cap:       数组容量
 * @count:     [输出] 实际返回条数
 *
 * ORDER BY trigger_ts DESC（最新在前），LIMIT ? 限制条数。
 * 当 device_id > 0 时绑定 2 个参数，否则只绑定 1 个（cap 的位置不同）。
 */
int db_alarm_query_recent(int device_id, alarm_record_t* out, int cap, int* count)
{
    sqlite3_stmt* stmt;
    int rc, n = 0;

    if (!out || !count || cap <= 0)
        return DB_ERR_PARAM;

    /* 两条 SQL 的占位符数量不同，分开 prepare */
    if (device_id > 0)
        rc = sqlite3_prepare_v2(g_db,
            "SELECT id,device_id,rule_id,level,description,trigger_ts,recover_ts,status"
            " FROM alarm_records WHERE device_id=?1"
            " ORDER BY trigger_ts DESC LIMIT ?2;", -1, &stmt, NULL);
    else
        rc = sqlite3_prepare_v2(g_db,
            "SELECT id,device_id,rule_id,level,description,trigger_ts,recover_ts,status"
            " FROM alarm_records ORDER BY trigger_ts DESC LIMIT ?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    /* 绑定参数：按设备过滤时 ?1=device_id ?2=cap；全部时 ?1=cap */
    if (device_id > 0) {
        sqlite3_bind_int(stmt, 1, device_id);
        sqlite3_bind_int(stmt, 2, cap);
    }
    else {
        sqlite3_bind_int(stmt, 1, cap);
    }

    while (n < cap && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        out[n].id = sqlite3_column_int(stmt, 0);
        out[n].device_id = sqlite3_column_int(stmt, 1);
        out[n].rule_id = sqlite3_column_type(stmt, 2) == SQLITE_NULL
            ? 0 : sqlite3_column_int(stmt, 2);
        out[n].level = sqlite3_column_int(stmt, 3);
        snprintf(out[n].description, sizeof(out[n].description), "%s",
            (const char*)sqlite3_column_text(stmt, 4));
        out[n].trigger_ts = sqlite3_column_int64(stmt, 5);
        out[n].recover_ts = sqlite3_column_type(stmt, 6) == SQLITE_NULL
            ? 0 : sqlite3_column_int64(stmt, 6);
        out[n].status = sqlite3_column_int(stmt, 7);
        n++;
    }
    sqlite3_finalize(stmt);
    *count = n;
    return (rc == SQLITE_DONE || rc == SQLITE_ROW) ? DB_OK : DB_ERR_SQL;
}

/*
 * db_alarm_count_open — 统计某设备未恢复的告警数
 * @device_id: 设备号
 * @count:     [输出] status=0 的记录数
 *
 * 用于界面显示未恢复告警数（红点/徽章），或判断是否需要弹窗提醒。
 */
int db_alarm_count_open(int device_id, int* count)
{
    sqlite3_stmt* stmt;
    int rc;

    if (!count)
        return DB_ERR_PARAM;

    rc = sqlite3_prepare_v2(g_db,
        "SELECT COUNT(*) FROM alarm_records WHERE status=0 AND device_id=?1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return DB_ERR_SQL;

    sqlite3_bind_int(stmt, 1, device_id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return DB_OK;
    }
    sqlite3_finalize(stmt);
    return DB_ERR_SQL;
}
