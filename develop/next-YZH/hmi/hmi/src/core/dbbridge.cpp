#include "dbbridge.h"
#include "datamanager.h"

extern "C" {
#include "storage.h"
#include <sqlite3.h>
}

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

DbBridge &DbBridge::instance()
{
    static DbBridge s;
    return s;
}

DbBridge::DbBridge(QObject *parent) : QObject(parent)
{
    m_flushTimer.setInterval(3000);   // 3s 攒批提交历史
    connect(&m_flushTimer, &QTimer::timeout, this, &DbBridge::flush);
}

bool DbBridge::open(const QString &dbFile)
{
    QDir().mkpath(QFileInfo(dbFile).absolutePath());
    if (db_open(dbFile.toLocal8Bit().constData()) != DB_OK)
        return false;
    m_open = true;

#ifdef HMI_LITE
    // 板上精简模式: 缩小 SQLite 页缓存(默认约2MB → 512KB), 降低常驻内存
    sqlite3_exec(db_handle(), "PRAGMA cache_size = -512;", nullptr, nullptr, nullptr);
#endif

    migrate();
    registerDevices();
    seedFromDb();
    m_flushTimer.start();
    return true;
}

void DbBridge::close()
{
    if (!m_open)
        return;
    flush();
    m_flushTimer.stop();
    db_close();
    m_open = false;
}

// 幂等迁移: 补齐 HMI 需要而成员 D 表结构没有的部分(详见 merge 文档问题清单 #1/#2)
void DbBridge::migrate()
{
    char *err = nullptr;
    // ① snapshots 表(抓拍索引) — D 的建表脚本中没有
    sqlite3_exec(db_handle(),
        "CREATE TABLE IF NOT EXISTS snapshots ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  device_id INTEGER NOT NULL,"
        "  alarm_id INTEGER,"
        "  reason TEXT NOT NULL DEFAULT '',"
        "  path TEXT NOT NULL,"
        "  ts INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_snap_dev_ts ON snapshots(device_id, ts);",
        nullptr, nullptr, &err);
    if (err) sqlite3_free(err);

    // ② alarm_records 补 type 列(0温度 1湿度 2抽检事件 3离线)
    if (!columnExists("alarm_records", "type")) {
        err = nullptr;
        sqlite3_exec(db_handle(),
            "ALTER TABLE alarm_records ADD COLUMN type INTEGER NOT NULL DEFAULT 0;",
            nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
    }
}

bool DbBridge::columnExists(const char *table, const char *column)
{
    sqlite3_stmt *stmt = nullptr;
    char sql[128];
    snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);
    if (sqlite3_prepare_v2(db_handle(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name && strcmp(name, column) == 0) { found = true; break; }
    }
    sqlite3_finalize(stmt);
    return found;
}

void DbBridge::registerDevices()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QList<int> ids = DataManager::instance().deviceIds();
    for (int id : ids) {
        device_info_t dev;
        memset(&dev, 0, sizeof(dev));
        dev.device_id = id;
        const QByteArray name = DataManager::instance().deviceName(id).toUtf8();
        strncpy(dev.name, name.constData(), sizeof(dev.name) - 1);
        strncpy(dev.group_name, "车间", sizeof(dev.group_name) - 1);
        strncpy(dev.ip, "", sizeof(dev.ip) - 1);
        dev.registered_at = now.toSecsSinceEpoch();

        device_info_t got;
        if (db_device_get(id, &got) == DB_OK)
            db_device_update(&dev);
        else
            db_device_insert(&dev);
    }
}

// 启动回灌: 最近 100 条告警/事件 + 最近 100 张抓拍 → DataManager 内存
void DbBridge::seedFromDb()
{
    // ---- 告警/事件 (type 列由本层迁移补齐) ----
    sqlite3_stmt *stmt = nullptr;
    QList<AlarmItem> items;
    if (sqlite3_prepare_v2(db_handle(),
        "SELECT id,device_id,level,description,trigger_ts,recover_ts,status,type"
        " FROM alarm_records ORDER BY trigger_ts DESC LIMIT 100;",
        -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AlarmItem a;
            a.id = sqlite3_column_int(stmt, 0);
            a.deviceId = sqlite3_column_int(stmt, 1);
            a.level = sqlite3_column_int(stmt, 2);
            a.message = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 3));
            a.raised = sqlite3_column_int64(stmt, 4) * 1000LL;
            a.restored = sqlite3_column_type(stmt, 5) == SQLITE_NULL
                ? 0 : sqlite3_column_int64(stmt, 5) * 1000LL;
            const int status = sqlite3_column_int(stmt, 6);
            a.active = (status == 0);
            a.type = sqlite3_column_int(stmt, 7);
            a.deviceName = DataManager::instance().deviceName(a.deviceId);
            if (a.type == SnapEvent) {
                a.active = false;               // 事件不作为"告警中"
                if (!a.restored) a.restored = a.raised;
            }
            items.append(a);
        }
    }
    sqlite3_finalize(stmt);
    DataManager::instance().seedAlarms(items);

    // ---- 抓拍 ----
    QList<SnapItem> snaps;
    if (sqlite3_prepare_v2(db_handle(),
        "SELECT device_id,reason,path,ts FROM snapshots ORDER BY ts DESC LIMIT 100;",
        -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SnapItem s;
            s.deviceId = sqlite3_column_int(stmt, 0);
            s.reason = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 1));
            s.path = QString::fromUtf8((const char *)sqlite3_column_text(stmt, 2));
            s.t = sqlite3_column_int64(stmt, 3) * 1000LL;
            snaps.append(s);
        }
    }
    sqlite3_finalize(stmt);
    DataManager::instance().seedSnapshots(snaps);
}

// ---------------- 数据落库 ----------------

void DbBridge::onDeviceUpdated(int deviceId)
{
    if (!m_open)
        return;
    const DeviceData d = DataManager::instance().device(deviceId);

    // 实时表即时 UPSERT(看板秒刷数据源); 离线时 run_status=0 标记
    sensor_sample_t s;
    memset(&s, 0, sizeof(s));
    s.device_id = deviceId;
    s.temperature = d.temp;
    s.humidity = d.humi;
    s.run_status = d.online ? 1 : 0;
    s.output_count = d.output;
    s.sample_ts = d.ts / 1000;
    db_realtime_upsert(&s);

    // 历史表: 在线才落, 攒批提交
    if (d.online)
        m_batch.append(s);
    if (m_batch.size() > 600)   // 防御: 异常情况下限制缓冲
        flush();
}

void DbBridge::flush()
{
    if (!m_open || m_batch.isEmpty())
        return;
    if (db_begin() != DB_OK) {
        m_batch.clear();        // 锁冲突: 放弃本批, 避免无限膨胀
        return;
    }
    for (int i = 0; i < m_batch.size(); ++i)
        db_history_insert(&m_batch[i]);
    if (db_commit() != DB_OK)
        db_rollback();
    m_batch.clear();
}

void DbBridge::onAlarmRaised(const AlarmItem &item)
{
    if (!m_open)
        return;
    alarm_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.device_id = item.deviceId;
    rec.rule_id = 0;             // HMI 规则引擎未用规则表, 关联字段留空
    rec.level = item.level;
    rec.trigger_ts = item.raised / 1000;
    rec.status = item.active ? 0 : 1;
    if (!item.active && item.restored > 0)
        rec.recover_ts = item.restored / 1000;
    const QByteArray msg = item.message.toUtf8();
    strncpy(rec.description, msg.constData(), sizeof(rec.description) - 1);

    if (db_alarm_insert(&rec) != DB_OK)
        return;
    m_hmiToDb[item.id] = rec.id;
    if (item.active && item.level >= 2)
        m_lastCrit[item.deviceId] = qMakePair(item.raised, rec.id);

    // 同步 type 列(D 的接口不覆盖该列, 直接 UPDATE)
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_handle(),
        "UPDATE alarm_records SET type=?1 WHERE id=?2;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, item.type);
        sqlite3_bind_int(stmt, 2, rec.id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void DbBridge::onAlarmRestored(const AlarmItem &item)
{
    if (!m_open)
        return;
    // seed 的记录 hmiId 即 db id; 运行期新增的查映射
    const int dbId = m_hmiToDb.value(item.id, item.id);
    if (dbId <= 0)
        return;
    db_alarm_update_status(dbId, item.active ? 0 : 1,
                           item.restored > 0 ? item.restored / 1000 : 0);
}

int DbBridge::lastCritAlarmDbId(int deviceId, qint64 nowMs)
{
    const QMap<int, QPair<qint64, int> >::const_iterator it = m_lastCrit.constFind(deviceId);
    if (it == m_lastCrit.constEnd())
        return 0;
    if (nowMs - it.value().first > 5000)   // 5s 关联窗口
        return 0;
    return it.value().second;
}

void DbBridge::onSnapshotTaken(int deviceId)
{
    if (!m_open)
        return;
    const SnapItem s = DataManager::instance().lastSnapshot(deviceId);
    if (s.path.isEmpty())
        return;

    const int alarmId = s.reason.startsWith("告警")
        ? lastCritAlarmDbId(deviceId, s.t) : 0;
    const QByteArray reason = s.reason.toUtf8();
    const QByteArray path = s.path.toUtf8();

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_handle(),
        "INSERT INTO snapshots(device_id,alarm_id,reason,path,ts)"
        " VALUES(?1,?2,?3,?4,?5);", -1, &stmt, nullptr) != SQLITE_OK)
        return;
    sqlite3_bind_int(stmt, 1, deviceId);
    if (alarmId > 0)
        sqlite3_bind_int(stmt, 2, alarmId);
    else
        sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, reason.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, path.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, s.t / 1000);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ---------------- 历史查询 ----------------

QVector<Sample> DbBridge::queryHistory(int deviceId, qint64 fromMs, qint64 toMs)
{
    QVector<Sample> out;
    if (!m_open)
        return out;

    const int64_t fromSec = fromMs / 1000;
    const int64_t toSec = toMs / 1000;
    int cap = int(toSec - fromSec) + 10;
    if (cap < 10) cap = 10;
    if (cap > 4000) cap = 4000;

    sensor_sample_t *arr = new sensor_sample_t[cap];
    int count = 0;
    if (db_history_query(deviceId, fromSec, toSec, arr, cap, &count) == DB_OK) {
        for (int i = 0; i < count; ++i) {
            Sample s;
            s.t = arr[i].sample_ts * 1000LL;
            s.temp = arr[i].temperature;
            s.humi = arr[i].humidity;
            s.output = (quint32)arr[i].output_count;
            out.append(s);
        }
    }
    delete[] arr;
    return out;
}
