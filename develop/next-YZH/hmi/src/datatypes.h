#pragma once
#include <QString>
#include <QMetaType>

// 一条采样记录(历史/实时共用)
struct Sample {
    qint64 t = 0;        // epoch 毫秒
    double temp = 0.0;   // 温度 °C
    int status = 0;      // 0=停机 1=运行
    quint32 output = 0;  // 累计产量
};

// 一台设备的实时快照
struct DeviceData {
    int id = 0;
    QString name;
    bool online = false;
    double temp = 0.0;
    int runStatus = 0;   // 0=停机 1=运行
    int alarmLevel = 0;  // 0=无 1=预警 2=告警 (由 DataManager 判定)
    quint32 output = 0;
    qint64 ts = 0;
};

// 一条告警记录
struct AlarmItem {
    quint32 id = 0;
    int deviceId = 0;
    QString deviceName;
    int level = 0;       // 1=预警 2=告警
    QString message;
    qint64 raised = 0;
    qint64 restored = 0;
    bool active = true;  // true=告警中 false=已恢复
};

Q_DECLARE_METATYPE(DeviceData)
Q_DECLARE_METATYPE(AlarmItem)
