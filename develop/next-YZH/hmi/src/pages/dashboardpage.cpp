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
#include <QPushButton>

DashboardPage::DashboardPage(QWidget *parent) : QWidget(parent)
{
    // ---- 顶部总览卡片 (5张: 在线/均温/均湿/产量/告警) ----
    m_cardOnline = new OverviewCard("在线设备", "台");
    m_cardTemp   = new OverviewCard("平均温度", "°C");
    m_cardHumi   = new OverviewCard("平均湿度", "%RH");
    m_cardOutput = new OverviewCard("今日产量", "件");
    m_cardAlarm  = new OverviewCard("活动告警", "条");
    QHBoxLayout *cards = new QHBoxLayout;
    cards->setSpacing(3);
    cards->addWidget(m_cardOnline);
    cards->addWidget(m_cardTemp);
    cards->addWidget(m_cardHumi);
    cards->addWidget(m_cardOutput);
    cards->addWidget(m_cardAlarm);

    // ---- 左: 设备状态列表 ----
    QFrame *devPanel = new QFrame;
    devPanel->setObjectName("panel");
    devPanel->setFixedWidth(138);
    QVBoxLayout *dl = new QVBoxLayout(devPanel);
    dl->setContentsMargins(3, 3, 3, 3);
    dl->setSpacing(2);
    QLabel *devTitle = new QLabel("采集点状态");
    devTitle->setObjectName("panelTitle");
    dl->addWidget(devTitle);
    m_devList = new DeviceList;
    dl->addWidget(m_devList, 1);

    // ---- 中: 温湿度趋势曲线 + 显示切换 ----
    QFrame *chartPanel = new QFrame;
    chartPanel->setObjectName("panel");
    QVBoxLayout *cl = new QVBoxLayout(chartPanel);
    cl->setContentsMargins(3, 3, 3, 3);
    cl->setSpacing(2);

    QHBoxLayout *mrow = new QHBoxLayout;
    mrow->setSpacing(3);
    m_btnTemp = new QPushButton("温度");
    m_btnTemp->setObjectName("chartBtn");
    m_btnTemp->setCheckable(true);
    m_btnTemp->setChecked(true);
    m_btnHumi = new QPushButton("湿度");
    m_btnHumi->setObjectName("chartBtn");
    m_btnHumi->setCheckable(true);
    mrow->addWidget(m_btnTemp);
    mrow->addWidget(m_btnHumi);
    mrow->addStretch();
    cl->addLayout(mrow);

    m_chart = new TrendChart;
    m_chart->setTitle("温度趋势 (°C)");
    m_chart->setYRange(15, 60);
    m_chart->setSlidingWindow(120);
    m_chart->addSeries("温度", Theme::Accent);
    m_chart->addSeries("湿度", QColor(0x00, 0xc8, 0x96));
    cl->addWidget(m_chart, 1);

    // ---- 右: 实时告警 + 消音 ----
    QFrame *alarmFrame = new QFrame;
    alarmFrame->setObjectName("panel");
    alarmFrame->setFixedWidth(150);
    QVBoxLayout *al = new QVBoxLayout(alarmFrame);
    al->setContentsMargins(3, 3, 3, 3);
    al->setSpacing(2);

    QHBoxLayout *arow = new QHBoxLayout;
    arow->setSpacing(3);
    QLabel *alarmTitle = new QLabel("实时告警");
    alarmTitle->setObjectName("panelTitle");
    m_btnMute = new QPushButton("消音");
    m_btnMute->setObjectName("chartBtn");
    arow->addWidget(alarmTitle);
    arow->addStretch();
    arow->addWidget(m_btnMute);
    al->addLayout(arow);

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
    connect(&dm, &DataManager::buzzerMuteChanged, this, &DashboardPage::onAlarmListChanged);
    connect(m_devList, &DeviceList::deviceSelected, this, &DashboardPage::onDeviceSelected);
    connect(m_btnTemp, &QPushButton::clicked, this, &DashboardPage::onModeTemp);
    connect(m_btnHumi, &QPushButton::clicked, this, &DashboardPage::onModeHumi);
    connect(m_btnMute, &QPushButton::clicked, this, &DashboardPage::onMute);

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

    if (id == m_selected && d.online) {
        m_tempPts.append(QPointF(d.ts, d.temp));
        m_humiPts.append(QPointF(d.ts, d.humi));
        while (m_tempPts.size() > 130) m_tempPts.removeFirst();
        while (m_humiPts.size() > 130) m_humiPts.removeFirst();
        applyMode();
    }
    refreshCards();
}

void DashboardPage::onAlarmListChanged()
{
    // 看板只显示"告警中"的条目(抽检/手动事件只在告警记录页查看)
    m_alarmPanel->setAlarms(DataManager::instance().activeAlarms());
    const bool muted = DataManager::instance().buzzerMuted(-1);
    m_btnMute->setText(muted ? "已消音" : "消音");
    m_btnMute->setEnabled(!muted);
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

void DashboardPage::onModeTemp()
{
    m_mode = 0;
    m_btnTemp->setChecked(true);
    m_btnHumi->setChecked(false);
    applyMode();
}

void DashboardPage::onModeHumi()
{
    m_mode = 1;
    m_btnHumi->setChecked(true);
    m_btnTemp->setChecked(false);
    applyMode();
}

void DashboardPage::onMute()
{
    // 全部消音 5 分钟 (真实系统: HMI → 服务端 0x03 下行命令 → 采集板关蜂鸣器)
    DataManager::instance().muteBuzzer(-1, 300);
}

void DashboardPage::applyMode()
{
    if (m_mode == 0) {
        m_chart->setSeriesData(0, m_tempPts);
        m_chart->setSeriesData(1, QVector<QPointF>());
        m_chart->setYRange(15, 60);
        m_chart->setTitle(QString("%1 · 温度趋势 (°C)")
                          .arg(DataManager::instance().deviceName(m_selected)));
    } else {
        m_chart->setSeriesData(0, QVector<QPointF>());
        m_chart->setSeriesData(1, m_humiPts);
        m_chart->setYRange(20, 100);
        m_chart->setTitle(QString("%1 · 湿度趋势 (%RH)")
                          .arg(DataManager::instance().deviceName(m_selected)));
    }
}

void DashboardPage::refreshCards()
{
    DataManager &dm = DataManager::instance();
    const QList<int> ids = dm.deviceIds();
    int online = 0;
    double sumT = 0.0, sumH = 0.0;
    for (int id : ids) {
        DeviceData d = dm.device(id);
        if (d.online) { ++online; sumT += d.temp; sumH += d.humi; }
    }

    m_cardOnline->setValue(QString("%1/%2").arg(online).arg(ids.size()));
    m_cardOnline->setSub(QString("共 %1 个采集点").arg(ids.size()));

    m_cardTemp->setValue(online ? QString::number(sumT / online, 'f', 1) : "--");
    m_cardTemp->setSub("在线采集点均值");

    m_cardHumi->setValue(online ? QString::number(sumH / online, 'f', 0) : "--");
    m_cardHumi->setSub("在线采集点均值");

    const quint32 out = dm.totalOutput();
    m_cardOutput->setValue(out >= 10000
        ? QString::number(out / 10000.0, 'f', 1) + "w" : QString::number(out));
    m_cardOutput->setSub(QString("红外计数 · 抽检 %1 次").arg(dm.snapEventCount()));

    const int n = dm.activeAlarmCount();
    m_cardAlarm->setValue(QString::number(n));
    m_cardAlarm->setSub(n > 0 ? "需要立即处理!" : "系统运行正常");
    m_cardAlarm->setAlarm(n > 0);
}

void DashboardPage::loadChartFor(int id)
{
    DataManager &dm = DataManager::instance();
    const QVector<Sample> h = dm.recentHistory(id, 120);
    m_tempPts.clear();
    m_humiPts.clear();
    for (int i = 0; i < h.size(); ++i) {
        m_tempPts.append(QPointF(h.at(i).t, h.at(i).temp));
        m_humiPts.append(QPointF(h.at(i).t, h.at(i).humi));
    }
    applyMode();
}
