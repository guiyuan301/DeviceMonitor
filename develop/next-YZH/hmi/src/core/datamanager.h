#pragma once
#include <QObject>
#include <QList>
#include <QMap>
#include <QVector>
#include <QPair>
#include "../datatypes.h"

/*
 * 数据中枢 V3: 设备实时值 / 历史序列 / 告警与事件 / 抓拍索引 全部存在这里,
 * 界面只与它打交道。
 *
 * 产量 = 红外对管计数: 采集板 1Hz 上报累计件数, DataManager 检测计数跨越
 * "每 N 件"的里程碑时发 snapshotRequested() 信号 → (真实系统)服务端向采集板
 * 下发抓拍命令 / (模拟器)生成模拟抓拍图 → 图片回到 addSnapshot() 存档。
 *
 * 真实接入(见 docs/项目文档-V3.md):
 *  - 服务端解析 0x01 数据上报 → DeviceData → onDeviceData()
 *  - 心跳超时/恢复            → setDeviceOnline(id, false/true)
 *  - 抓拍图收齐(0x12)         → addSnapshot(id, path, reason)
 *  - 本信号 → 服务端发 0x04 抓拍命令 / 模拟器直接生成图片:
 *     snapshotRequested(devid, reason)
 */
class DataManager : public QObject
{
    Q_OBJECT
public:
    static DataManager &instance();

    void initDevices(const QList<QPair<int, QString> > &devices);

    QList<int> deviceIds() const;
    DeviceData device(int id) const;
    QString deviceName(int id) const;

    // 某采集点最近 N 秒历史
    QVector<Sample> recentHistory(int id, int seconds) const;

    QList<AlarmItem> alarms() const;        // 全部(含抽检事件), 时间倒序
    QList<AlarmItem> activeAlarms() const;  // 仅告警中(看板用, 不含事件)
    int activeAlarmCount() const;
    quint32 totalOutput() const;
    int snapEventCount() const;

    SnapItem lastSnapshot(int deviceId) const;  // deviceId<=0 取最新任意
    int snapshotCount() const;
    bool buzzerMuted(int deviceId) const;

public slots:
    // 数据入口: 每采集点 1Hz 一次 (产量计数跨越抽检里程碑时自动发 snapshotRequested)
    void onDeviceData(const DeviceData &data);
    // 服务端心跳超时/恢复时调用
    void setDeviceOnline(int id, bool on);
    // 抓拍图落盘后调用 (reason: "抽检·第N件"/"告警留档"/"手动")
    void addSnapshot(int deviceId, const QString &jpegPath, const QString &reason);
    // 消音: deviceId<0 表示全部; 持续 seconds 秒
    void muteBuzzer(int deviceId, int seconds);
    // 确认并清除全部当前告警
    void acknowledgeAlarms();

signals:
    void deviceUpdated(int id);
    void alarmRaised(const AlarmItem &item);
    void alarmRestored(const AlarmItem &item);
    void alarmListChanged();
    // 请求抓拍: 真实系统→服务端转发0x04命令给采集板; 模拟器→直接生成图片
    void snapshotRequested(int deviceId, const QString &reason);
    void snapshotTaken(int deviceId);
    void buzzerMuteChanged();

private:
    explicit DataManager(QObject *parent = nullptr);

    void evaluateEnv(DeviceData &d);
    void raiseOffline(const DeviceData &d);
    void restoreType(int deviceId, int type);
    AlarmItem *findActive(int deviceId, int type);
    bool buzzerShouldRing(const DeviceData &d) const;

    QMap<int, DeviceData> m_devices;
    QMap<int, QVector<Sample> > m_history;
    QList<AlarmItem> m_alarms;
    QMap<int, SnapItem> m_lastSnap;
    QList<SnapItem> m_snaps;
    QMap<int, qint64> m_muteUntil;   // deviceId -> 消音截止 epoch ms
    quint32 m_nextAlarmId = 1;

    enum {
        kMaxSamples = 3600,   // 每点保留 1 小时 (1Hz)
        kMaxAlarms  = 300,
        kMaxSnaps   = 100,
        kSnapEveryN = 10,     // 每 N 件抽检抓拍一次
        kTempWarn   = 35,     // °C
        kTempCrit   = 45,
        kHumiWarn   = 70,     // %RH
        kHumiCrit   = 85,
        kHumiLow    = 30,
        kSnapLinkMs = 5000    // 抓拍与告警的时间关联窗口
    };
};
