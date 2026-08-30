#pragma once
#include <QObject>
#include <QList>
#include <QMap>
#include <QVector>
#include <QPair>
#include "../datatypes.h"

/*
 * 数据中枢: 所有设备实时值 / 历史序列 / 告警记录都存在这里,
 * 界面只与它打交道。真实接入时, 成员 A 的服务端解析出数据后,
 * 调用 onDeviceData() (跨线程用信号/队列连接过渡), 界面通过
 * deviceUpdated()/alarmRaised() 信号刷新 —— 与数据来源解耦。
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

    // 取某设备最近 N 秒的历史采样
    QVector<Sample> recentHistory(int id, int seconds) const;

    QList<AlarmItem> alarms() const;          // 全部, 按时间倒序
    QList<AlarmItem> activeAlarms() const;    // 仅告警中
    int activeAlarmCount() const;
    quint32 totalOutput() const;

public slots:
    // 数据入口: 每台设备每次采样调用一次 (1Hz)
    void onDeviceData(const DeviceData &data);
    // 全部确认/消除当前告警 (界面演示用)
    void acknowledgeAlarms();

signals:
    void deviceUpdated(int id);
    void alarmRaised(const AlarmItem &item);
    void alarmRestored(const AlarmItem &item);
    void alarmListChanged();

private:
    explicit DataManager(QObject *parent = nullptr);
    void evaluateAlarm(DeviceData &d);

    QMap<int, DeviceData> m_devices;
    QMap<int, QVector<Sample> > m_history;
    QList<AlarmItem> m_alarms;
    quint32 m_nextAlarmId = 1;

    enum {
        kMaxSamples  = 3600,  // 每设备保留 1 小时 (1Hz)
        kWarnTemp    = 60,    // 预警阈值 °C
        kCritTemp    = 75,    // 告警阈值 °C
        kRestoreTemp = 58     // 恢复滞回阈值 °C
    };
};
