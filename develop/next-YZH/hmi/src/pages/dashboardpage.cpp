#include "dashboardpage.h"
#include "../core/datamanager.h"
#include "../widgets/overviewcard.h"
#include "../widgets/trendchart.h"
#include "../widgets/devicelist.h"
#include "../widgets/alarmpanel.h"
#include "../theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QTimer>

DashboardPage::DashboardPage(QWidget *parent) : QWidget(parent)
{
    // ---- 顶部总览卡片 ----
    m_cardOnline = new OverviewCard("在线设备", "台");
    m_cardTemp   = new OverviewCard("平均温度", "°C");
    m_cardOutput = new OverviewCard("累计产量", "件");
    m_cardAlarm  = new OverviewCard("活动告警", "条");
    QHBoxLayout *cards = new QHBoxLayout;
    cards->setSpacing(4);
    cards->addWidget(m_cardOnline);
    cards->addWidget(m_cardTemp);
    cards->addWidget(m_cardOutput);
    cards->addWidget(m_cardAlarm);

    // ---- 左: 设备状态列表 ----
    QFrame *devPanel = new QFrame;
    devPanel->setObjectName("panel");
    devPanel->setFixedWidth(128);
    QVBoxLayout *dl = new QVBoxLayout(devPanel);
    dl->setContentsMargins(3, 3, 3, 3);
    dl->setSpacing(2);
    QLabel *devTitle = new QLabel("设备状态");
    devTitle->setObjectName("panelTitle");
    dl->addWidget(devTitle);
    m_devList = new DeviceList;
    dl->addWidget(m_devList, 1);

    // ---- 中: 温度趋势曲线 ----
    QFrame *chartPanel = new QFrame;
    chartPanel->setObjectName("panel");
    QVBoxLayout *cl = new QVBoxLayout(chartPanel);
    cl->setContentsMargins(3, 3, 3, 3);
    cl->setSpacing(0);
    m_chart = new TrendChart;
    m_chart->setTitle("温度趋势 (°C)");
    m_chart->setYRange(20, 90);
    m_chart->setSlidingWindow(120);
    m_chart->addSeries("温度", Theme::Accent);
    cl->addWidget(m_chart);

    // ---- 右: 实时告警 ----
    QFrame *alarmFrame = new QFrame;
    alarmFrame->setObjectName("panel");
    alarmFrame->setFixedWidth(150);
    QVBoxLayout *al = new QVBoxLayout(alarmFrame);
    al->setContentsMargins(3, 3, 3, 3);
    al->setSpacing(2);
    QLabel *alarmTitle = new QLabel("实时告警");
    alarmTitle->setObjectName("panelTitle");
    al->addWidget(alarmTitle);
    m_alarmPanel = new AlarmPanel;
    al->addWidget(m_alarmPanel, 1);

    QHBoxLayout *body = new QHBoxLayout;
    body->setSpacing(4);
    body->addWidget(devPanel);
    body->addWidget(chartPanel, 1);
    body->addWidget(alarmFrame);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(4, 4, 4, 4);
    main->setSpacing(4);
    main->addLayout(cards);
    main->addLayout(body, 1);

    DataManager &dm = DataManager::instance();
    const QList<int> ids = dm.deviceIds();
    for (int id : ids)
        m_devList->addDevice(id, dm.deviceName(id));

    connect(&dm, &DataManager::deviceUpdated, this, &DashboardPage::onDeviceUpdated);
    connect(&dm, &DataManager::alarmRaised, this, &DashboardPage::onAlarmListChanged);
    connect(&dm, &DataManager::alarmRestored, this, &DashboardPage::onAlarmListChanged);
    connect(&dm, &DataManager::alarmListChanged, this, &DashboardPage::onAlarmListChanged);
    connect(m_devList, &DeviceList::deviceSelected, this, &DashboardPage::onDeviceSelected);

    // 告警设备 LED 闪烁节拍
    QTimer *blink = new QTimer(this);
    blink->setInterval(500);
    connect(blink, &QTimer::timeout, this, &DashboardPage::onBlink);
    blink->start();

    if (!ids.isEmpty())
        m_selected = ids.first();
    loadChartFor(m_selected);
    onAlarmListChanged();
    refreshCards();
}

void DashboardPage::onDeviceUpdated(int id)
{
    DeviceData d = DataManager::instance().device(id);
    m_devList->updateDevice(d);

    if (id == m_selected) {
        if (d.online) {
            m_points.append(QPointF(d.ts, d.temp));
            while (m_points.size() > 130)
                m_points.removeFirst();
        }
        m_chart->setSeriesData(0, m_points);
    }
    refreshCards();
}

void DashboardPage::onAlarmListChanged()
{
    m_alarmPanel->setAlarms(DataManager::instance().alarms());
    refreshCards();
}

void DashboardPage::onDeviceSelected(int id)
{
    m_selected = id;
    loadChartFor(id);
}

void DashboardPage::onBlink()
{
    m_blink = !m_blink;
    m_devList->setBlink(m_blink);
}

void DashboardPage::refreshCards()
{
    DataManager &dm = DataManager::instance();
    const QList<int> ids = dm.deviceIds();
    int online = 0;
    double sum = 0.0;
    for (int id : ids) {
        DeviceData d = dm.device(id);
        if (d.online) { ++online; sum += d.temp; }
    }

    m_cardOnline->setValue(QString("%1/%2").arg(online).arg(ids.size()));
    m_cardOnline->setSub(QString("共 %1 台设备").arg(ids.size()));

    m_cardTemp->setValue(online ? QString::number(sum / online, 'f', 1) : "--");
    m_cardTemp->setSub("在线设备均值");

    const quint32 out = dm.totalOutput();
    m_cardOutput->setValue(out >= 10000
        ? QString::number(out / 10000.0, 'f', 1) + "w" : QString::number(out));
    m_cardOutput->setSub("全部设备合计");

    const int n = dm.activeAlarmCount();
    m_cardAlarm->setValue(QString::number(n));
    m_cardAlarm->setSub(n > 0 ? "需要立即处理!" : "系统运行正常");
    m_cardAlarm->setAlarm(n > 0);
}

void DashboardPage::loadChartFor(int id)
{
    DataManager &dm = DataManager::instance();
    const QVector<Sample> h = dm.recentHistory(id, 120);
    m_points.clear();
    for (int i = 0; i < h.size(); ++i)
        m_points.append(QPointF(h.at(i).t, h.at(i).temp));
    m_chart->setSeriesData(0, m_points);
    m_chart->setTitle(QString("%1 · 温度趋势 (°C)").arg(dm.deviceName(id)));
}
