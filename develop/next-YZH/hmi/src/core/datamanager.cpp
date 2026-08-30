#include "datamanager.h"
#include <QDateTime>
#include <algorithm>

DataManager &DataManager::instance()
{
    static DataManager s;
    return s;
}

DataManager::DataManager(QObject *parent) : QObject(parent)
{
}

void DataManager::initDevices(const QList<QPair<int, QString> > &devices)
{
    m_devices.clear();
    m_history.clear();
    for (int i = 0; i < devices.size(); ++i) {
        DeviceData d;
        d.id = devices.at(i).first;
        d.name = devices.at(i).second;
        m_devices.insert(d.id, d);
        m_history.insert(d.id, QVector<Sample>());
    }
}

QList<int> DataManager::deviceIds() const
{
    return m_devices.keys();
}

DeviceData DataManager::device(int id) const
{
    return m_devices.value(id);
}

QString DataManager::deviceName(int id) const
{
    return m_devices.value(id).name;
}

QVector<Sample> DataManager::recentHistory(int id, int seconds) const
{
    const QVector<Sample> &v = m_history.value(id);
    QVector<Sample> out;
    if (v.isEmpty())
        return out;
    const qint64 from = v.last().t - qint64(seconds) * 1000;
    for (int i = 0; i < v.size(); ++i)
        if (v.at(i).t >= from)
            out.append(v.at(i));
    return out;
}

QList<AlarmItem> DataManager::alarms() const
{
    QList<AlarmItem> out = m_alarms;
    std::sort(out.begin(), out.end(),
              [](const AlarmItem &a, const AlarmItem &b) { return a.raised > b.raised; });
    return out;
}

QList<AlarmItem> DataManager::activeAlarms() const
{
    QList<AlarmItem> out;
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms.at(i).active)
            out.append(m_alarms.at(i));
    std::sort(out.begin(), out.end(),
              [](const AlarmItem &a, const AlarmItem &b) { return a.raised > b.raised; });
    return out;
}

int DataManager::activeAlarmCount() const
{
    int n = 0;
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms.at(i).active)
            ++n;
    return n;
}

quint32 DataManager::totalOutput() const
{
    quint32 sum = 0;
    QMap<int, DeviceData>::const_iterator it = m_devices.constBegin();
    for (; it != m_devices.constEnd(); ++it)
        sum += it.value().output;
    return sum;
}

void DataManager::onDeviceData(const DeviceData &data)
{
    if (!m_devices.contains(data.id))
        return;

    DeviceData d = data;
    d.alarmLevel = 0;

    // 追加历史环形缓冲
    Sample s;
    s.t = data.ts;
    s.temp = data.temp;
    s.status = data.runStatus;
    s.output = data.output;
    QVector<Sample> &h = m_history[data.id];
    h.append(s);
    while (h.size() > kMaxSamples)
        h.removeFirst();

    evaluateAlarm(d);

    m_devices[d.id] = d;
    emit deviceUpdated(d.id);
}

// 阈值判定: >=75 告警 / >=60 预警, 回落到 58 以下恢复(滞回防抖)
void DataManager::evaluateAlarm(DeviceData &d)
{
    AlarmItem *active = nullptr;
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms[i].active && m_alarms[i].deviceId == d.id) {
            active = &m_alarms[i];
            break;
        }

    int level = 0;
    if (d.online && d.temp >= kCritTemp)
        level = 2;
    else if (d.online && d.temp >= kWarnTemp)
        level = 1;

    if (level > 0) {
        if (!active) {
            AlarmItem item;
            item.id = m_nextAlarmId++;
            item.deviceId = d.id;
            item.deviceName = d.name;
            item.level = level;
            item.raised = QDateTime::currentMSecsSinceEpoch();
            item.active = true;
            item.message = (level >= 2)
                    ? QString("温度超高 %1°C (阈值 %2°C)").arg(d.temp, 0, 'f', 1).arg(kCritTemp)
                    : QString("温度偏高 %1°C (阈值 %2°C)").arg(d.temp, 0, 'f', 1).arg(kWarnTemp);
            m_alarms.append(item);
            d.alarmLevel = level;
            emit alarmRaised(item);
            emit alarmListChanged();
        } else {
            // 已有活动告警: 级别只升不降, 消息同步更新
            if (level > active->level) {
                active->level = level;
                active->message = QString("温度超高 %1°C (阈值 %2°C)")
                                      .arg(d.temp, 0, 'f', 1).arg(kCritTemp);
                emit alarmListChanged();
            }
            d.alarmLevel = active->level;
        }
    } else if (active && (!d.online || d.temp <= kRestoreTemp)) {
        active->active = false;
        active->restored = QDateTime::currentMSecsSinceEpoch();
        emit alarmRestored(*active);
        emit alarmListChanged();
    }
}

void DataManager::acknowledgeAlarms()
{
    bool any = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms[i].active) {
            m_alarms[i].active = false;
            m_alarms[i].restored = now;
            m_devices[m_alarms[i].deviceId].alarmLevel = 0;
            emit deviceUpdated(m_alarms[i].deviceId);
            any = true;
        }
    if (any)
        emit alarmListChanged();
}
