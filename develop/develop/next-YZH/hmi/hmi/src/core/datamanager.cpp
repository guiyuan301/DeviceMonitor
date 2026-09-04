#include "datamanager.h"
#include <QDateTime>
#include <QTimer>
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

int DataManager::snapEventCount() const
{
    int n = 0;
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms.at(i).type == SnapEvent)
            ++n;
    return n;
}

SnapItem DataManager::lastSnapshot(int deviceId) const
{
    if (deviceId > 0)
        return m_lastSnap.value(deviceId);
    return m_snaps.isEmpty() ? SnapItem() : m_snaps.first();
}

int DataManager::snapshotCount() const
{
    return m_snaps.size();
}

bool DataManager::buzzerMuted(int deviceId) const
{
    if (deviceId < 0) {
        for (QMap<int, qint64>::const_iterator it = m_muteUntil.constBegin();
             it != m_muteUntil.constEnd(); ++it) {
            if (it.value() > QDateTime::currentMSecsSinceEpoch())
                return true;
        }
        return false;
    }
    return m_muteUntil.value(deviceId) > QDateTime::currentMSecsSinceEpoch();
}

void DataManager::onDeviceData(const DeviceData &data)
{
    if (!m_devices.contains(data.id))
        return;

    const DeviceData prev = m_devices.value(data.id);
    DeviceData d = data;
    d.alarmLevel = 0;
    d.buzzerOn = false;

    // 在线才落历史(离线期间传感器无数据)
    if (d.online) {
        Sample s;
        s.t = d.ts;
        s.temp = d.temp;
        s.humi = d.humi;
        s.output = d.output;
        QVector<Sample> &h = m_history[d.id];
        h.append(s);
        while (h.size() > kMaxSamples)
            h.removeFirst();
    }

    // 离线/恢复跃变 → 离线告警
    if (prev.online && !d.online)
        raiseOffline(d);
    else if (!prev.online && d.online)
        restoreType(d.id, OfflineAlarm);

    // 产量抽检: 计数跨越"每 N 件"里程碑 → 请求抓拍
    if (d.online && d.output > prev.output) {
        const quint32 markPrev = prev.output / kSnapEveryN;
        const quint32 markNow = d.output / kSnapEveryN;
        if (markNow > markPrev)
            emit snapshotRequested(d.id, QString("抽检·第%1件").arg(markNow * kSnapEveryN));
    }

    // 环境温湿度阈值告警
    evaluateEnv(d);

    // 蜂鸣器联动: 环境告警(级别2)中且未消音
    d.buzzerOn = buzzerShouldRing(d);

    m_devices[d.id] = d;
    emit deviceUpdated(d.id);
}

void DataManager::setDeviceOnline(int id, bool on)
{
    if (!m_devices.contains(id))
        return;
    DeviceData d = m_devices.value(id);
    if (d.online == on)
        return;
    d.online = on;
    d.ts = QDateTime::currentMSecsSinceEpoch();
    onDeviceData(d);
}

void DataManager::addSnapshot(int deviceId, const QString &jpegPath, const QString &reason)
{
    SnapItem s;
    s.deviceId = deviceId;
    s.t = QDateTime::currentMSecsSinceEpoch();
    s.reason = reason;
    s.path = jpegPath;
    m_lastSnap[deviceId] = s;
    m_snaps.prepend(s);
    while (m_snaps.size() > kMaxSnaps)
        m_snaps.removeLast();

    if (reason.startsWith("告警")) {
        // 告警留档: 关联该点 5 秒内最新的级别2告警
        AlarmItem *best = nullptr;
        for (int i = 0; i < m_alarms.size(); ++i) {
            AlarmItem &a = m_alarms[i];
            if (!a.active || a.deviceId != deviceId || a.level < 2)
                continue;
            if (s.t - a.raised > kSnapLinkMs)
                continue;
            if (!best || a.raised > best->raised)
                best = &a;
        }
        if (best)
            best->hasSnap = true;
    } else {
        // 抽检/手动: 生成一条事件记录(非告警, 仅留档)
        AlarmItem item;
        item.id = m_nextAlarmId++;
        item.deviceId = deviceId;
        item.deviceName = m_devices.value(deviceId).name;
        item.type = SnapEvent;
        item.level = 1;
        item.message = QString("%1 · 抓拍留档").arg(reason);
        item.raised = s.t;
        item.restored = s.t;
        item.active = false;
        item.hasSnap = true;
        m_alarms.append(item);
        while (m_alarms.size() > kMaxAlarms)
            m_alarms.removeFirst();
    }
    emit alarmListChanged();
    emit snapshotTaken(deviceId);
}

void DataManager::muteBuzzer(int deviceId, int seconds)
{
    const qint64 until = QDateTime::currentMSecsSinceEpoch() + qint64(seconds) * 1000;
    if (deviceId < 0) {
        for (QMap<int, DeviceData>::iterator it = m_devices.begin();
             it != m_devices.end(); ++it)
            m_muteUntil[it.key()] = until;
    } else {
        m_muteUntil[deviceId] = until;
    }
    emit buzzerMuteChanged();
}

void DataManager::acknowledgeAlarms()
{
    bool any = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms[i].active) {
            m_alarms[i].active = false;
            m_alarms[i].restored = now;
            emit alarmRestored(m_alarms[i]);   // 带完整数据, 供数据库同步
            any = true;
        }
    QMap<int, DeviceData>::iterator it = m_devices.begin();
    for (; it != m_devices.end(); ++it) {
        it.value().alarmLevel = 0;
        it.value().buzzerOn = false;
        emit deviceUpdated(it.key());
    }
    if (any)
        emit alarmListChanged();
}

void DataManager::seedAlarms(const QList<AlarmItem> &items)
{
    m_alarms = items;
    quint32 maxId = 0;
    for (int i = 0; i < items.size(); ++i)
        if (items.at(i).id > maxId)
            maxId = items.at(i).id;
    if (m_nextAlarmId <= maxId)
        m_nextAlarmId = maxId + 1;
    if (!items.isEmpty())
        emit alarmListChanged();
}

void DataManager::seedSnapshots(const QList<SnapItem> &items)
{
    m_snaps = items;
    m_lastSnap.clear();
    // items 按时间倒序, 每台设备第一条即最新
    for (int i = 0; i < items.size(); ++i)
        if (!m_lastSnap.contains(items.at(i).deviceId))
            m_lastSnap.insert(items.at(i).deviceId, items.at(i));
}

// ---------------- 内部 ----------------

AlarmItem *DataManager::findActive(int deviceId, int type)
{
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms[i].active && m_alarms[i].deviceId == deviceId
            && m_alarms[i].type == type)
            return &m_alarms[i];
    return nullptr;
}

void DataManager::restoreType(int deviceId, int type)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool any = false;
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms[i].active && m_alarms[i].deviceId == deviceId
            && m_alarms[i].type == type) {
            m_alarms[i].active = false;
            m_alarms[i].restored = now;
            emit alarmRestored(m_alarms[i]);   // 带完整数据, 供数据库同步
            any = true;
        }
    if (any)
        emit alarmListChanged();
}

// 环境告警: 一台同时只保留一条(温度/湿度取更严重的), 恢复即置已恢复
void DataManager::evaluateEnv(DeviceData &d)
{
    int level = 0;
    int type = TempAlarm;
    QString msg;

    if (d.online) {
        if (d.temp >= kTempCrit) {
            level = 2; type = TempAlarm;
            msg = QString("温度超高 %1°C (阈值 %2°C)").arg(d.temp, 0, 'f', 1).arg(kTempCrit);
        } else if (d.humi >= kHumiCrit) {
            level = 2; type = HumiAlarm;
            msg = QString("湿度超高 %1%RH (阈值 %2%RH)").arg(d.humi, 0, 'f', 0).arg(kHumiCrit);
        } else if (d.temp >= kTempWarn) {
            level = 1; type = TempAlarm;
            msg = QString("温度偏高 %1°C (阈值 %2°C)").arg(d.temp, 0, 'f', 1).arg(kTempWarn);
        } else if (d.humi >= kHumiWarn) {
            level = 1; type = HumiAlarm;
            msg = QString("湿度偏高 %1%RH (阈值 %2%RH)").arg(d.humi, 0, 'f', 0).arg(kHumiWarn);
        } else if (d.humi > 1.0 && d.humi <= kHumiLow) {
            level = 1; type = HumiAlarm;
            msg = QString("湿度过低 %1%RH (下限 %2%RH)").arg(d.humi, 0, 'f', 0).arg(kHumiLow);
        }
    }

    AlarmItem *a = findActive(d.id, TempAlarm);
    if (!a)
        a = findActive(d.id, HumiAlarm);

    if (level > 0) {
        if (!a) {
            AlarmItem item;
            item.id = m_nextAlarmId++;
            item.deviceId = d.id;
            item.deviceName = d.name;
            item.type = type;
            item.level = level;
            item.message = msg;
            item.raised = QDateTime::currentMSecsSinceEpoch();
            item.active = true;
            m_alarms.append(item);
            while (m_alarms.size() > kMaxAlarms)
                m_alarms.removeFirst();
            d.alarmLevel = level;
            emit alarmRaised(item);
            emit alarmListChanged();
            // 级别2告警首次触发 → 请求抓拍留档
            if (level >= 2)
                emit snapshotRequested(d.id, "告警留档");
        } else {
            if (a->level != level || a->type != type || a->message != msg) {
                a->level = level;
                a->type = type;
                a->message = msg;
                emit alarmListChanged();
            }
            d.alarmLevel = level;
        }
    } else if (a) {
        a->active = false;
        a->restored = QDateTime::currentMSecsSinceEpoch();
        emit alarmRestored(*a);
        emit alarmListChanged();
    }
}

void DataManager::raiseOffline(const DeviceData &d)
{
    AlarmItem item;
    item.id = m_nextAlarmId++;
    item.deviceId = d.id;
    item.deviceName = d.name;
    item.type = OfflineAlarm;
    item.level = 1;
    item.message = "设备离线 (心跳超时)";
    item.raised = QDateTime::currentMSecsSinceEpoch();
    item.active = true;
    m_alarms.append(item);
    while (m_alarms.size() > kMaxAlarms)
        m_alarms.removeFirst();
    emit alarmRaised(item);
    emit alarmListChanged();
}

bool DataManager::buzzerShouldRing(const DeviceData &d) const
{
    if (buzzerMuted(d.id))
        return false;
    for (int i = 0; i < m_alarms.size(); ++i)
        if (m_alarms[i].active && m_alarms[i].deviceId == d.id
            && m_alarms[i].level >= 2)
            return true;
    return false;
}
