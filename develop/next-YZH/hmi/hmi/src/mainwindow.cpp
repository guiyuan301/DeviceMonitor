#include "mainwindow.h"
#include "theme.h"
#include "core/datamanager.h"
#include "core/datasimulator.h"
#include "core/serverclient.h"
#include "core/dbbridge.h"
#include "pages/dashboardpage.h"
#include "pages/historypage.h"
#include "pages/videopage.h"
#include "pages/recordspage.h"
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QDateTime>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QSettings>

MainWindow::~MainWindow()
{
    // 退出前把剩余批量数据落库并关闭数据库
    DbBridge::instance().close();
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle("车间设备集中监控系统");
    setFixedSize(480, 272);

    // ---- 数据采集点注册 (先于页面创建; 4板部署: 1中央板 + 3采集板) ----
    QList<QPair<int, QString> > devs;
    devs << qMakePair(1, QStringLiteral("车间A"))
         << qMakePair(2, QStringLiteral("车间B"))
         << qMakePair(3, QStringLiteral("仓库C"));
    DataManager::instance().initDevices(devs);

    // ---- 数据库(成员D XS_/db 模块, V4合并) ----
    // 打开 + 迁移 + 设备注册 + 历史回灌; 之后所有告警/抓拍/采样自动落库
    DbBridge &db = DbBridge::instance();
    if (db.open(QCoreApplication::applicationDirPath() + "/monitor.db")) {
        connect(&DataManager::instance(), &DataManager::deviceUpdated,
                &db, &DbBridge::onDeviceUpdated);
        connect(&DataManager::instance(), &DataManager::alarmRaised,
                &db, &DbBridge::onAlarmRaised);
        connect(&DataManager::instance(), &DataManager::alarmRestored,
                &db, &DbBridge::onAlarmRestored);
        connect(&DataManager::instance(), &DataManager::snapshotTaken,
                &db, &DbBridge::onSnapshotTaken);
    }

    // ---- 顶栏 ----
    QFrame *topBar = new QFrame;
    topBar->setObjectName("topBar");
    topBar->setFixedHeight(26);

    QLabel *title = new QLabel("车间设备集中监控系统");
    QFont tf = title->font();
    tf.setPixelSize(11);
    tf.setBold(true);
    title->setFont(tf);
    title->setStyleSheet(QString("color:%1;").arg(Theme::Accent.name()));

    m_netLed = new QLabel;
    m_netLed->setFixedSize(8, 8);
    m_netLed->setStyleSheet(QString("background:%1; border-radius:4px;")
                            .arg(Theme::Warn.name()));
    m_netLabel = new QLabel("数据源: 模拟");
    m_netLabel->setObjectName("netLabel");

    m_clock = new QLabel;
    m_clock->setObjectName("clockLabel");

    QHBoxLayout *tl = new QHBoxLayout(topBar);
    tl->setContentsMargins(8, 0, 8, 0);
    tl->setSpacing(4);
    tl->addWidget(title);
    tl->addStretch();
    tl->addWidget(m_netLed);
    tl->addWidget(m_netLabel);
    tl->addSpacing(8);
    tl->addWidget(m_clock);

    // ---- 页面堆栈 ----
    m_stack = new QStackedWidget;
    m_stack->addWidget(new DashboardPage);  // 0 实时看板
    m_stack->addWidget(new HistoryPage);    // 1 历史回查
    m_stack->addWidget(new VideoPage);      // 2 视频监控
    m_stack->addWidget(new RecordsPage);    // 3 告警记录

    // ---- 底部导航 ----
    QFrame *navBar = new QFrame;
    navBar->setObjectName("navBar");
    navBar->setFixedHeight(26);

    QPushButton *btnDash = new QPushButton("实时看板");
    QPushButton *btnHist = new QPushButton("历史回查");
    QPushButton *btnVideo = new QPushButton("视频监控");
    QPushButton *btnAlarm = new QPushButton("告警记录");
    QList<QPushButton *> navs;
    navs << btnDash << btnHist << btnVideo << btnAlarm;
    for (int i = 0; i < navs.size(); ++i) {
        navs.at(i)->setObjectName("navBtn");
        navs.at(i)->setCheckable(true);
    }
    btnDash->setChecked(true);

    QButtonGroup *grp = new QButtonGroup(this);
    grp->setExclusive(true);
    for (int i = 0; i < navs.size(); ++i) {
        grp->addButton(navs.at(i), i);
        connect(navs.at(i), &QPushButton::clicked, this, [this, i]() {
            m_stack->setCurrentIndex(i);
        });
    }

    QHBoxLayout *nl = new QHBoxLayout(navBar);
    nl->setContentsMargins(0, 0, 0, 0);
    nl->setSpacing(0);
    for (int i = 0; i < navs.size(); ++i)
        nl->addWidget(navs.at(i));
    nl->addStretch();

    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);
    main->addWidget(topBar);
    main->addWidget(m_stack, 1);
    main->addWidget(navBar);

    // ---- 数据源二选一: 服务端真实数据 / 模拟器 ----
    /* QSettings 键(配置文件里可改):
     *   server/enabled = true/false   是否连服务端(板上默认 true, Windows 默认 false)
     *   server/ip      = x.x.x.x      服务端 IP(队友中央板地址)
     *   server/port    = 8888         服务端端口(与 service/central 的 server_main 一致) */
    QSettings settings;
    const bool useServer = settings.value("server/enabled",
#if defined(Q_OS_LINUX)
                                          true      // 板上: 默认连真实服务端
#else
                                          false     // Windows 开发机: 默认模拟器演示
#endif
                                          ).toBool();

    if (useServer) {
        /* 真实链路: 采集板 → 服务端(转发补丁) → 本客户端 → DataManager
         * 模拟器不再启动, 抓拍请求也不回灌假图(真实抓拍只来自视频页手动抓拍) */
        m_client = new ServerClient(this);
        connect(m_client, &ServerClient::deviceData,
                &DataManager::instance(), &DataManager::onDeviceData);
        
        // 连接服务端推送的数据到DbBridge，写入本地数据库
        connect(m_client, &ServerClient::realtimeDataReceived,
                &DbBridge::instance(), &DbBridge::onRealtimeDataReceived);
        connect(m_client, &ServerClient::deviceInfoReceived,
                &DbBridge::instance(), &DbBridge::onDeviceInfoReceived);

        // 连接状态 → 顶栏 LED 变色 + 断线时全部采集点标离线(触发离线告警)
        connect(m_client, &ServerClient::connectedChanged, this, [this](bool on) {
            m_netLed->setStyleSheet(QString("background:%1; border-radius:4px;")
                                    .arg(on ? "#00c896" : "#e74c3c"));
            if (!on) {
                const QList<int> ids = DataManager::instance().deviceIds();
                for (int id : ids)
                    DataManager::instance().setDeviceOnline(id, false);
            }
        });
        // 连接状态文本 → 顶栏"数据源:xxx"
        connect(m_client, &ServerClient::statusText, this, [this](const QString &t) {
            m_netLabel->setText(QString("数据源: %1").arg(t));
        });

        m_client->start(settings.value("server/ip", "192.168.1.100").toString(),
                        quint16(settings.value("server/port", 8888).toUInt()));
    } else {
        // 模拟链路(原 V3 行为): 定时器 1Hz 产 3 个采集点数据
        m_sim = new DataSimulator(this);
        connect(m_sim, &DataSimulator::deviceData,
                &DataManager::instance(), &DataManager::onDeviceData);
        // 抓拍请求: 真实系统改为 connect 到 server_send_cmd(devid, 0x04手动抓拍)
        connect(&DataManager::instance(), &DataManager::snapshotRequested,
                m_sim, &DataSimulator::onSnapshotRequested);
        m_sim->start(1000);
        m_netLabel->setText("数据源: 模拟");
    }

    QTimer *clockTimer = new QTimer(this);
    clockTimer->setInterval(1000);
    connect(clockTimer, &QTimer::timeout, this, &MainWindow::onClock);
    clockTimer->start();
    onClock();
}

void MainWindow::onClock()
{
    m_clock->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
}
