#pragma once
#include <QObject>
#include <QTimer>
#include <QMap>
#include <QVector>
#include <QPair>
#include "../datatypes.h"

extern "C" {
#include "storage.h"   // sensor_sample_t (成员D 数据结构)
}

/*
 * DbBridge — HMI 与 成员D数据库模块(XS_/db) 的桥接层 (V4 合并新增)
 *
 * 职责:
 *  1. 打开/迁移数据库: 用成员 D 的 db_open() 建表(WAL), 再补 HMI 需要的
 *     扩展(snapshots 表 + alarm_records.type 列) —— 迁移幂等, 见 merge 文档;
 *  2. 数据落库: deviceUpdated → 实时表即时 UPSERT + 历史表攒批事务提交(3s);
 *     alarmRaised/Restored → alarm_records 插入/恢复;
 *     snapshotTaken → snapshots 插入;
 *  3. 启动回灌: alarm_records/snapshots 最近记录 → DataManager::seed*, 重启不丢记录;
 *  4. 历史回查: HistoryPage 走本层 queryHistory(毫秒区间, 内部转成员D的秒)。
 *
 * 线程约定(遵循成员 D storage.h): 所有 db_* 调用都在主线程串行执行,
 * 批量提交用事务; 服务端接入后写库应移交服务端入库线程(见 merge 文档遗留项)。
 */
class DbBridge : public QObject
{
    Q_OBJECT
public:
    static DbBridge &instance();

    bool open(const QString &dbFile);
    void close();
    bool isOpen() const { return m_open; }

    // 按设备+毫秒时间区间查历史(数据库无数据时返回空, 调用方可回退内存)
    QVector<Sample> queryHistory(int deviceId, qint64 fromMs, qint64 toMs);

public slots:
    void onDeviceUpdated(int deviceId);
    void onAlarmRaised(const AlarmItem &item);
    void onAlarmRestored(const AlarmItem &item);
    void onSnapshotTaken(int deviceId);
    void flush();   // 定时批量提交历史缓冲

private:
    explicit DbBridge(QObject *parent = nullptr);

    void migrate();           // 幂等: snapshots 表 + alarm_records.type 列
    void registerDevices();   // devices 表注册/更新全部采集点
    void seedFromDb();        // 启动回灌告警与抓拍到 DataManager
    bool columnExists(const char *table, const char *column);
    int  lastCritAlarmDbId(int deviceId, qint64 nowMs);

    bool m_open = false;
    QTimer m_flushTimer;                        // 3s 批量提交历史
    QVector<sensor_sample_t> m_batch;           // 历史表批量缓冲
    QMap<qint64, int> m_hmiToDb;                // HMI告警id -> alarm_records.id
    QMap<int, QPair<qint64, int> > m_lastCrit;  // devid -> (最近级别2告警 raisedMs, dbId)
};
