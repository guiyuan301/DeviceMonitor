#include "datasimulator.h"
#include "datamanager.h"
#include <QDateTime>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QCoreApplication>
#include <QtGlobal>

DataSimulator::DataSimulator(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &DataSimulator::tick);

    struct Init { int id; const char *name; double tBase; double hBase; };
    const Init inits[] = {
        { 1, "车间A", 28.0, 55.0 },   // 注塑区: 演示高温告警
        { 2, "车间B", 30.0, 58.0 },   // 装配区: 产线出件频繁
        { 3, "仓库C", 24.0, 65.0 },   // 仓库: 演示高湿告警
    };
    qsrand(uint(QDateTime::currentMSecsSinceEpoch()));
    for (int i = 0; i < int(sizeof(inits) / sizeof(inits[0])); ++i) {
        Sim s;
        s.cur.id = inits[i].id;
        s.cur.name = QString::fromUtf8(inits[i].name);
        s.cur.online = true;
        s.cur.temp = inits[i].tBase + (qrand() % 100 - 50) / 20.0;
        s.cur.humi = inits[i].hBase + (qrand() % 100 - 50) / 10.0;
        s.cur.output = 100 + qrand() % 400;   // 起始已有一些累计产量
        s.cur.ts = QDateTime::currentMSecsSinceEpoch();
        s.tBase = inits[i].tBase;
        s.hBase = inits[i].hBase;
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

QString DataSimulator::makeSnapshot(const DeviceData &d, const QString &reason)
{
#ifdef HMI_DISABLE_SNAPSHOT
    // 板上精简模式(HMI_LITE): 不生成抓拍图, 节省内存与存储
    Q_UNUSED(d); Q_UNUSED(reason);
    return QString();
#else
    QImage img(320, 240, QImage::Format_RGB32);
    QPainter p(&img);
    p.fillRect(img.rect(), QColor(0x14, 0x1d, 0x26));
    // 模拟"画面": 随机噪点 + 边框
    for (int i = 0; i < 2600; ++i) {
        int x = qrand() % 320, y = qrand() % 240;
        int g = 40 + qrand() % 70;
        p.setPen(QColor(g, g + 6, g + 12));
        p.drawPoint(x, y);
    }
    p.setPen(QPen(QColor(0x00, 0xa8, 0xff), 2));
    p.drawRect(img.rect().adjusted(1, 1, -1, -1));
    QFont f = p.font();
    f.setPixelSize(15);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0x00, 0xc8, 0x96));
    p.drawText(30, 40, QString("【%1】%2").arg(reason).arg(d.name));
    f.setPixelSize(12);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor(0xd8, 0xe2, 0xea));
    p.drawText(30, 70, QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    p.drawText(30, 92, QString("温度 %1°C   湿度 %2%RH   产量 %3件")
               .arg(d.temp, 0, 'f', 1).arg(d.humi, 0, 'f', 0).arg(d.output));
    p.setPen(QColor(0x7c, 0x8d, 0x9c));
    p.drawText(30, 214, "模拟抓拍 · 接入摄像头后为真实画面");
    p.end();

    const QString dir = QCoreApplication::applicationDirPath() + "/snaps";
    QDir().mkpath(dir);
    const QString path = QString("%1/snap_%2_%3.jpg")
                         .arg(dir).arg(d.id)
                         .arg(QDateTime::currentMSecsSinceEpoch());
    img.save(path, "JPEG", 85);
    return path;
#endif   // HMI_DISABLE_SNAPSHOT
}

void DataSimulator::onSnapshotRequested(int deviceId, const QString &reason)
{
#ifdef HMI_DISABLE_SNAPSHOT
    // 板上精简模式: 直接忽略抓拍请求
    Q_UNUSED(deviceId); Q_UNUSED(reason);
#else
    for (int i = 0; i < m_sims.size(); ++i) {
        if (m_sims[i].cur.id != deviceId)
            continue;
        const QString path = makeSnapshot(m_sims[i].cur, reason);
        if (path.isEmpty())
            return;
        DataManager::instance().addSnapshot(deviceId, path, reason);
        return;
    }
#endif
}

void DataSimulator::tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < m_sims.size(); ++i) {
        Sim &s = m_sims[i];
        DeviceData &d = s.cur;

        // 偶发掉线 / 恢复
        if (d.online && qrand() % 500 == 0)
            d.online = false;
        else if (!d.online && qrand() % 8 == 0)
            d.online = true;

        if (d.online) {
            // 温度随机游走 + 回归; 车间A(索引0)演示高温尖峰
            d.temp += (qrand() % 100 - 50) / 100.0 * 1.6 + (s.tBase - d.temp) * 0.08;
            if (i == 0) {
                if (s.tSpikeLeft > 0) { --s.tSpikeLeft; d.temp += 1.8; }
                else if (qrand() % 150 == 0) s.tSpikeLeft = 10 + qrand() % 10;
            }
            // 湿度随机游走 + 回归; 仓库C(索引2)演示高湿尖峰
            d.humi += (qrand() % 100 - 50) / 100.0 * 1.2 + (s.hBase - d.humi) * 0.08;
            if (i == 2) {
                if (s.hSpikeLeft > 0) { --s.hSpikeLeft; d.humi += 1.8; }
                else if (qrand() % 150 == 0) s.hSpikeLeft = 8 + qrand() % 8;
            }

            // 产量: 红外对管计数, 产线匀速偏快出件 (约 0.5~1 件/秒)
            if (qrand() % 100 < 60)
                ++d.output;
        }

        d.ts = now;
        emit deviceData(d);
    }
}
