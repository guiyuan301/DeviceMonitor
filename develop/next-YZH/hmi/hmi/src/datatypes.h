#pragma once
#include <QString>
#include <QMetaType>

// 告警/事件类型
enum AlarmType {
    TempAlarm    = 0,   // 温度越限
    HumiAlarm    = 1,   // 湿度越限
    SnapEvent    = 2,   // 抓拍事件(产量抽检/手动抓拍), 非告警, 仅记录
    OfflineAlarm = 3    // 设备离线(心跳超时)
};

inline QString alarmTypeName(int type)
{
    switch (type) {
    case TempAlarm:    return "温度";
    case HumiAlarm:    return "湿度";
    case SnapEvent:    return "抽检";
    case OfflineAlarm: return "离线";
    }
    return "未知";
}

// 一条采样记录(历史/实时共用)
struct Sample {
    qint64 t = 0;
    double temp = 0.0;
    double humi = 0.0;
    quint32 output = 0;    // 累计产量(件), 红外对管计数
};

// 一台采集点(采集板)的实时快照
struct DeviceData {
    int id = 0;
    QString name;
    bool online = false;
    double temp = 0.0;
    double humi = 0.0;
    int alarmLevel = 0;    // 0=无 1=预警 2=告警 (由 DataManager 判定)
    bool buzzerOn = false; // 蜂鸣器(DataManager 联动计算: 环境告警中且未消音)
    quint32 output = 0;    // 累计产量(件)
    qint64 ts = 0;
};

// 一条告警/事件记录
struct AlarmItem {
    quint32 id = 0;
    int deviceId = 0;
    QString deviceName;
    int type = TempAlarm;  // AlarmType
    int level = 0;         // 1=预警 2=告警 (SnapEvent 类型仅作记录)
    QString message;
    qint64 raised = 0;
    qint64 restored = 0;
    bool active = true;
    bool hasSnap = false;  // 是否关联了抓拍图
};

// 一次摄像头抓拍
struct SnapItem {
    int deviceId = 0;
    qint64 t = 0;
    QString reason;        // "抽检·第N件" / "告警留档" / "手动"
    QString path;          // jpeg 文件路径
};

Q_DECLARE_METATYPE(DeviceData)
Q_DECLARE_METATYPE(AlarmItem)
Q_DECLARE_METATYPE(SnapItem)
