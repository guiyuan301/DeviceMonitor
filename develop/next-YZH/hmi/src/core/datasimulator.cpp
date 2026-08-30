#include "datasimulator.h"
#include <QDateTime>
#include <QtGlobal>

DataSimulator::DataSimulator(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &DataSimulator::tick);

    struct Init { int id; const char *name; double base; };
    const Init inits[] = {
        { 1, "注塑机A",   45.0 },
        { 2, "注塑机B",   42.0 },
        { 3, "数控车床C", 38.0 },
        { 4, "空压机D",   50.0 },
        { 5, "烘干炉E",   62.0 },
        { 6, "包装线F",   40.0 },
    };
    qsrand(uint(QDateTime::currentMSecsSinceEpoch()));
    for (int i = 0; i < int(sizeof(inits) / sizeof(inits[0])); ++i) {
        Sim s;
        s.cur.id = inits[i].id;
        s.cur.name = QString::fromUtf8(inits[i].name);
        s.cur.online = true;
        s.cur.runStatus = 1;
        s.cur.temp = inits[i].base + (qrand() % 100 - 50) / 20.0;
        s.cur.output = 100 + qrand() % 900;
        s.cur.ts = QDateTime::currentMSecsSinceEpoch();
        s.base = inits[i].base;
        m_sims.append(s);
    }
}

void DataSimulator::start(int intervalMs)
{
    m_timer->setInterval(intervalMs);
    m_timer->start();
}

void DataSimulator::stop()
{
    m_timer->stop();
}

bool DataSimulator::isRunning() const
{
    return m_timer->isActive();
}

void DataSimulator::tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < m_sims.size(); ++i) {
        Sim &s = m_sims[i];
        DeviceData &d = s.cur;

        // 偶发掉线(约1/400每秒), 数秒~十几秒后恢复
        if (d.online && qrand() % 400 == 0)
            d.online = false;
        else if (!d.online && qrand() % 8 == 0)
            d.online = true;

        // 偶发启停
        if (d.online && qrand() % 150 == 0)
            d.runStatus = d.runStatus ? 0 : 1;

        if (d.online) {
            // 温度随机游走 + 向基准回归
            const double drift = (qrand() % 100 - 50) / 100.0 * 2.0;
            d.temp += drift + (s.base - d.temp) * 0.08;
            // 烘干炉(索引4)更易出现高温尖峰, 便于演示超限告警
            if (i == 4) {
                if (s.spikeLeft > 0) {
                    --s.spikeLeft;
                    d.temp += 2.2;
                } else if (qrand() % 120 == 0) {
                    s.spikeLeft = 8 + qrand() % 10;
                }
            }
            if (d.runStatus)
                d.output += quint32(qrand() % 4);
        }

        d.ts = now;
        emit deviceData(d);
    }
}
