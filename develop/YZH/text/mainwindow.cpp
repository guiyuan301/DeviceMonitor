#include "mainwindow.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTimer>
#include <QDateTime>
#include <QPainter>
#include <QFontMetrics>
#include <QStatusBar>
#include <QGuiApplication>
#include <QScreen>

// ===========================================================================
// StatCard：顶部总览统计卡片
// ===========================================================================
StatCard::StatCard(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_value(QStringLiteral("--"))
    , m_valueColor(QColor("#e8f0f8"))
    , m_accent(QColor("#00c8d7"))
    , m_flash(false)
{
    setMinimumSize(120, 62);
}

void StatCard::setValueText(const QString &text, const QColor &color)
{
    m_value = text;
    m_valueColor = color;
    update();
}

void StatCard::setAccent(const QColor &color)
{
    m_accent = color;
    update();
}

void StatCard::setFlash(bool on)
{
    m_flash = on;
    update();
}

void StatCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rc = rect().adjusted(1, 1, -2, -2);

    // ---- 自适应缩放：以 120x62 的"标准卡片"为基准，小屏等比缩小 ----
    const double s = qBound(0.45, qMin(rc.width() / 120.0, rc.height() / 62.0), 1.3);

    // 底板：告警计数 > 0 时卡片底色微微泛红 + 闪烁边框
    QColor bg("#16202c");
    QColor border("#263445");
    if (m_flash) {
        bg = QColor("#2c1a1e");
        border = QColor("#ff4d4d");
    }
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(rc, 8, 8);
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rc.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    // 左侧竖向强调条（不同卡片不同主题色，区分指标类型）
    p.setPen(Qt::NoPen);
    p.setBrush(m_accent);
    p.drawRoundedRect(QRectF(rc.left() + 6 * s, rc.top() + 10 * s,
                             3 * s, rc.height() - 20 * s), 1.5, 1.5);

    // 标题（小字）
    QFont f = font();
    f.setPixelSize(qMax(7, int(10 * s)));
    p.setFont(f);
    p.setPen(QColor("#8fa3b8"));
    p.drawText(QRectF(rc.left() + 16 * s, rc.top() + 7 * s,
                      rc.width() - 20 * s, 14 * s),
               Qt::AlignVCenter | Qt::AlignLeft, m_title);

    // 数值（大字）
    f.setPixelSize(qMax(10, int(19 * s)));
    f.setBold(true);
    p.setFont(f);
    p.setPen(m_valueColor);
    p.drawText(QRectF(rc.left() + 16 * s, rc.top() + 21 * s,
                      rc.width() - 22 * s, rc.height() - 25 * s),
               Qt::AlignVCenter | Qt::AlignLeft, m_value);
}

// ===========================================================================
// MainWindow
// ===========================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_cardTotal(0)
    , m_cardOnline(0)
    , m_cardRun(0)
    , m_cardAlarm(0)
    , m_cardOutput(0)
    , m_cardTemp(0)
    , m_chart(0)
    , m_alarmList(0)
    , m_timeLabel(0)
    , m_linkLabel(0)
    , m_alarmBadge(0)
    , m_selectedId(-1)
    , m_compact(false)
{
    setWindowTitle(QString::fromUtf8("车间设备数据采集与可视化监控系统"));
    resize(1080, 660);
    setMinimumSize(860, 560);

    // ---- 小屏自适应：板载 480x272 屏自动切紧凑模式（无边框全屏） ----
    //     桌面调试可加 --compact 参数强制预览小屏效果
    const QRect screen = QGuiApplication::primaryScreen()->geometry();
    const bool forceCompact = QCoreApplication::arguments().contains("--compact");
    if (screen.width() <= 700 || screen.height() <= 350 || forceCompact) {
        m_compact = true;
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setMinimumSize(0, 0);
        resize(forceCompact ? QSize(480, 272) : screen.size());
    }

    applyGlobalStyle();
    buildUi();

    // ---------- 模拟设备初始化（后续替换为真实数据源） ----------
    // 故意设计了一个演示剧本：
    //   106 烘干炉 基准温度贴着阈值 80，运行中会周期性触发/恢复告警
    //   103 贴片机 处于停机状态
    //   108 输送线 离线（模拟拔网线）
    struct Seed { int id; const char *name; double base; int state; };
    const Seed seeds[] = {
        { 101, "注塑机-01",  62.0, DeviceCard::StateRunning },
        { 102, "CNC车床-02", 55.0, DeviceCard::StateRunning },
        { 103, "贴片机-03",  46.0, DeviceCard::StateIdle    },
        { 104, "空压机-04",  70.0, DeviceCard::StateRunning },
        { 105, "包装机-05",  44.0, DeviceCard::StateRunning },
        { 106, "烘干炉-06",  79.5, DeviceCard::StateRunning },  // 贴着阈值，演示周期性告警
        { 107, "检测台-07",  52.0, DeviceCard::StateRunning },
        { 108, "输送线-08",  40.0, DeviceCard::StateOffline },  // 离线演示
    };
    const int seedCount = int(sizeof(seeds) / sizeof(seeds[0]));
    for (int i = 0; i < seedCount; ++i) {
        DeviceSim d;
        d.id = seeds[i].id;
        d.name = QString::fromUtf8(seeds[i].name);
        d.baseTemp = seeds[i].base;
        d.temp = seeds[i].base;
        d.state = seeds[i].state;
        d.output = 200 + (qrand() % 800);   // 初始产量随机
        d.alarm = false;
        m_devices.append(d);
    }

    // 默认选中第一台设备展示趋势
    m_selectedId = m_devices.first().id;
    m_chart->showDevice(m_selectedId, m_devices.first().name);
    m_cards.value(m_selectedId)->setSelected(true);

    // ---------- 定时器 ----------
    connect(&m_tickTimer, SIGNAL(timeout()), this, SLOT(onTick()));
    connect(&m_flashTimer, SIGNAL(timeout()), this, SLOT(onFlash()));
    m_tickTimer.start(1000);    // 与协议上报周期 1s 对齐
    m_flashTimer.start(500);

    qsrand(uint(QDateTime::currentMSecsSinceEpoch()));
    onTick();                   // 立即刷一帧，避免启动时空白
}

void MainWindow::applyGlobalStyle()
{
    // 深色工业风全局样式（QSS）
    setStyleSheet(QString::fromUtf8(
        "QMainWindow { background: #0b1118; }"
        "QWidget { font-family: \"Microsoft YaHei\"; color: #cfd8e3; }"
        "QLabel { background: transparent; }"
        "QListWidget {"
        "  background: #101822; border: 1px solid #263445; border-radius: 6px;"
        "  outline: 0; padding: 2px;"
        "}"
        "QListWidget::item { border-bottom: 1px solid #1a2532; padding: 1px; }"
        "QListWidget::item:selected { background: #1a2735; }"
        "QScrollBar:vertical { background: #101822; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #2c3d50; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ));
}

QWidget *makePanel(const QString &objectName)
{
    QWidget *w = new QWidget;
    w->setObjectName(objectName);
    w->setStyleSheet(QString::fromUtf8(
        "QWidget#%1 { background: #121a25; border: 1px solid #263445; border-radius: 8px; }"
        "QWidget#%1 QLabel { border: none; }").arg(objectName));
    return w;
}

void MainWindow::buildUi()
{
    QWidget *central = new QWidget;
    central->setStyleSheet("background: #0b1118;");
    setCentralWidget(central);

    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(m_compact ? 4 : 10, m_compact ? 4 : 8,
                             m_compact ? 4 : 10, m_compact ? 4 : 8);
    root->setSpacing(m_compact ? 4 : 8);

    // ================= 1. 顶栏 =================
    QWidget *header = new QWidget;
    header->setFixedHeight(m_compact ? 22 : 40);
    header->setStyleSheet(
        "background: #121a25; border: 1px solid #263445; border-radius: 8px;");
    QHBoxLayout *hb = new QHBoxLayout(header);
    hb->setContentsMargins(14, 2, 14, 2);
    hb->setSpacing(8);

    QLabel *logo = new QLabel(QChar(0x25CF));   // 圆点当 logo 装饰
    logo->setStyleSheet(m_compact ? "color: #2ecc71; font-size: 10px;"
                                  : "color: #2ecc71; font-size: 14px;");
    QLabel *title = new QLabel(QString::fromUtf8("车间设备监控系统"));
    title->setStyleSheet(m_compact
        ? "color: #e8f0f8; font-size: 11px; font-weight: bold; border: none;"
        : "color: #e8f0f8; font-size: 15px; font-weight: bold; border: none;");
    QLabel *sub = new QLabel(QString::fromUtf8("中央板 · 中央监控大屏"));
    sub->setStyleSheet("color: #5a6b7d; font-size: 10px; border: none;");
    sub->setVisible(!m_compact);                // 小屏隐藏副标题，省空间

    m_linkLabel = new QLabel;
    m_linkLabel->setStyleSheet(m_compact ? "color: #2ecc71; font-size: 9px; border: none;"
                                         : "color: #2ecc71; font-size: 11px; border: none;");
    m_timeLabel = new QLabel;
    m_timeLabel->setStyleSheet(m_compact ? "color: #8fa3b8; font-size: 9px; border: none;"
                                         : "color: #8fa3b8; font-size: 11px; border: none;");

    hb->addWidget(logo);
    hb->addWidget(title);
    hb->addWidget(sub);
    hb->addStretch();
    hb->addWidget(m_linkLabel);
    hb->addWidget(m_timeLabel);
    root->addWidget(header);

    // ================= 2. 总览统计卡片 =================
    QHBoxLayout *statRow = new QHBoxLayout;
    statRow->setSpacing(m_compact ? 4 : 8);
    m_cardTotal  = new StatCard(QString::fromUtf8("设备总数"));
    m_cardOnline = new StatCard(QString::fromUtf8("在线设备"));
    m_cardRun    = new StatCard(QString::fromUtf8("运行中"));
    m_cardAlarm  = new StatCard(QString::fromUtf8("告警"));
    m_cardOutput = new StatCard(QString::fromUtf8("今日总产量"));
    m_cardTemp   = new StatCard(QString::fromUtf8("平均温度"));

    m_cardTotal->setAccent(QColor("#00c8d7"));
    m_cardOnline->setAccent(QColor("#3498db"));
    m_cardRun->setAccent(QColor("#2ecc71"));
    m_cardAlarm->setAccent(QColor("#e74c3c"));
    m_cardOutput->setAccent(QColor("#9b59b6"));
    m_cardTemp->setAccent(QColor("#f39c12"));

    if (m_compact) {
        // 小屏：放开最小尺寸限制，让 6 张卡片挤进一行
        const QList<StatCard *> cards = { m_cardTotal, m_cardOnline, m_cardRun,
                                          m_cardAlarm, m_cardOutput, m_cardTemp };
        for (int i = 0; i < cards.size(); ++i)
            cards.at(i)->setMinimumSize(56, 36);
    }

    statRow->addWidget(m_cardTotal);
    statRow->addWidget(m_cardOnline);
    statRow->addWidget(m_cardRun);
    statRow->addWidget(m_cardAlarm);
    statRow->addWidget(m_cardOutput, 1);
    statRow->addWidget(m_cardTemp);
    root->addLayout(statRow);

    // ================= 3. 中部：设备网格 + 告警列表 =================
    QHBoxLayout *mid = new QHBoxLayout;
    mid->setSpacing(m_compact ? 4 : 8);

    // ---- 左：8 台设备卡片（4 列 x 2 行） ----
    QWidget *gridPanel = makePanel("gridPanel");
    QGridLayout *grid = new QGridLayout(gridPanel);
    grid->setContentsMargins(m_compact ? 4 : 8, m_compact ? 4 : 8,
                             m_compact ? 4 : 8, m_compact ? 4 : 8);
    grid->setSpacing(m_compact ? 4 : 8);

    for (int i = 0; i < 8; ++i) {
        // 先创建占位卡片，真实数据在 onTick() 首帧填充
        DeviceCard *card = new DeviceCard;
        if (m_compact) {
            card->setCompact(true);             // 小屏：信息完整的小字号布局
            card->setMinimumSize(72, 56);
        }
        connect(card, SIGNAL(clicked(int)), this, SLOT(onDeviceClicked(int)));
        m_cards.insert(i + 101, card);
        grid->addWidget(card, i / 4, i % 4);
    }
    mid->addWidget(gridPanel, 3);

    // ---- 右：实时告警面板 ----
    QWidget *alarmPanel = makePanel("alarmPanel");
    alarmPanel->setMinimumWidth(m_compact ? 120 : 270);
    alarmPanel->setMaximumWidth(m_compact ? 140 : 320);
    QVBoxLayout *ab = new QVBoxLayout(alarmPanel);
    ab->setContentsMargins(m_compact ? 6 : 10, m_compact ? 5 : 8,
                           m_compact ? 6 : 10, m_compact ? 6 : 10);
    ab->setSpacing(m_compact ? 3 : 6);

    QHBoxLayout *alarmHead = new QHBoxLayout;
    QLabel *alarmTitle = new QLabel(QString::fromUtf8("实时告警"));
    alarmTitle->setStyleSheet("color: #e8f0f8; font-size: 13px; font-weight: bold; border: none;");
    m_alarmBadge = new QLabel("0");
    m_alarmBadge->setAlignment(Qt::AlignCenter);
    m_alarmBadge->setFixedWidth(26);
    m_alarmBadge->setStyleSheet(
        "background: #3a4553; color: #cfd8e3; border-radius: 10px; font-size: 11px; font-weight: bold;");
    alarmHead->addWidget(alarmTitle);
    alarmHead->addStretch();
    alarmHead->addWidget(m_alarmBadge);
    ab->addLayout(alarmHead);

    m_alarmList = new QListWidget;
    m_alarmList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_alarmList->setSelectionMode(QAbstractItemView::NoSelection);
    m_alarmList->setStyleSheet(m_compact ? "font-size: 9px;" : "font-size: 11px;");
    ab->addWidget(m_alarmList, 1);
    mid->addWidget(alarmPanel, 1);

    root->addLayout(mid, 1);

    // ================= 4. 底部：趋势曲线 =================
    m_chart = new TrendChart;
    m_chart->setMinimumHeight(m_compact ? 56 : 150);
    root->addWidget(m_chart, 1);

    // ================= 5. 状态栏 =================
    statusBar()->setStyleSheet(
        "QStatusBar { background: #0e1620; color: #5a6b7d; font-size: 10px; }"
        "QStatusBar::item { border: none; }");
    if (m_compact) {
        // 小屏：隐藏状态栏，把宝贵的竖向空间让给设备卡片
        statusBar()->hide();
    } else {
        statusBar()->showMessage(
            QString::fromUtf8("就绪 | 模拟数据模式（阶段2）| 上报周期 1s | 心跳超时判定 15s"));
    }
}

// ===========================================================================
// 模拟数据驱动（1 秒 1 帧）—— 后续接入真数据时替换本函数
// ===========================================================================
void MainWindow::simulate()
{
    for (int i = 0; i < m_devices.size(); ++i) {
        DeviceSim &d = m_devices[i];

        if (d.state == DeviceCard::StateOffline)
            continue;   // 离线设备不上报

        // 温度随机行走：围绕基准值波动 + 5% 概率热尖峰（演示突发告警）
        d.temp += (qrand() % 9 - 4) * 0.6;
        d.temp += (d.baseTemp - d.temp) * 0.08;
        if (d.state == DeviceCard::StateRunning && qrand() % 100 < 3)
            d.temp += 12.0 + (qrand() % 8);
        d.temp = qBound(25.0, d.temp, 99.0);

        // 运行状态小概率切换（运行 <-> 停机）
        if (d.state != DeviceCard::StateAlarm && qrand() % 100 < 1)
            d.state = (d.state == DeviceCard::StateRunning)
                        ? DeviceCard::StateIdle : DeviceCard::StateRunning;

        // 运行中才计数产量
        if (d.state == DeviceCard::StateRunning)
            d.output += qrand() % 4;
    }
}

void MainWindow::onTick()
{
    simulate();

    const double alarmHigh = 80.0;  // 告警上限（对齐告警规则表）
    const double recoverTo = 78.0;  // 恢复迟滞，防抖

    int online = 0, running = 0, alarmCnt = 0, outputSum = 0;
    double tempSum = 0.0;

    for (int i = 0; i < m_devices.size(); ++i) {
        DeviceSim &d = m_devices[i];
        const bool offline = (d.state == DeviceCard::StateOffline);

        if (!offline) {
            ++online;
            tempSum += d.temp;
            outputSum += d.output;
            if (d.state == DeviceCard::StateRunning)
                ++running;

            // ---- 告警引擎（带迟滞的阈值判定，阶段4逻辑先内建演示） ----
            if (!d.alarm && d.temp >= alarmHigh) {
                d.alarm = true;
                d.state = DeviceCard::StateAlarm;
                pushAlarm(QString::fromUtf8("[%1] 温度超限 %2℃ (阈值 %3℃)")
                          .arg(d.name).arg(d.temp, 0, 'f', 1).arg(alarmHigh, 0, 'f', 0), 2);
            } else if (d.alarm && d.temp <= recoverTo) {
                d.alarm = false;
                d.state = DeviceCard::StateRunning;
                pushAlarm(QString::fromUtf8("[%1] 温度恢复 %2℃")
                          .arg(d.name).arg(d.temp, 0, 'f', 1), 3);
            }

            if (d.alarm)
                ++alarmCnt;
        }

        // ---- 刷新设备卡片 ----
        DeviceCard *card = m_cards.value(d.id);
        if (card) {
            card->setDevice(d.id, d.name);
            card->setState(d.state);
            card->setTemp(offline ? -9999.0 : d.temp);
            card->setOutput(d.output);
        }

        // ---- 喂趋势曲线（离线设备断线不画） ----
        if (!offline)
            m_chart->addSample(d.id, d.temp);
    }

    // ---- 刷新总览卡片 ----
    const int total = m_devices.size();
    const double avgTemp = online > 0 ? tempSum / online : 0.0;

    m_cardTotal->setValueText(QString::number(total), QColor("#e8f0f8"));
    m_cardOnline->setValueText(QString::fromUtf8("%1 / %2").arg(online).arg(total),
                               online == total ? QColor("#2ecc71") : QColor("#f1c40f"));
    m_cardRun->setValueText(QString::number(running), QColor("#2ecc71"));
    m_cardAlarm->setValueText(QString::number(alarmCnt),
                              alarmCnt > 0 ? QColor("#ff5252") : QColor("#2ecc71"));
    m_cardOutput->setValueText(QString::number(outputSum), QColor("#cfd8e3"));

    QColor avgColor = QColor("#e8f0f8");
    if (avgTemp >= 80.0) avgColor = QColor("#ff5252");
    else if (avgTemp >= 70.0) avgColor = QColor("#ffa726");
    m_cardTemp->setValueText(QString::number(avgTemp, 'f', 1) + QString::fromUtf8(" ℃"), avgColor);

    // ---- 顶栏时间 / 连接状态（小屏用短文案，防挤出屏幕） ----
    if (m_compact) {
        m_timeLabel->setText(QDateTime::currentDateTime().toString("MM-dd hh:mm"));
        m_linkLabel->setText(QString::fromUtf8("● 已连接"));
    } else {
        m_timeLabel->setText(QDateTime::currentDateTime().toString(
            QString::fromUtf8("yyyy-MM-dd hh:mm:ss")));
        m_linkLabel->setText(QString::fromUtf8("● 服务器 192.168.1.100:8888 已连接"));
    }
}

void MainWindow::onFlash()
{
    m_flashOn = !m_flashOn;

    // 只在"确实有告警"时闪烁，避免无告警时空闪
    bool anyAlarm = false;
    for (int i = 0; i < m_devices.size(); ++i) {
        const DeviceSim &d = m_devices.at(i);
        if (d.alarm) anyAlarm = true;
        DeviceCard *card = m_cards.value(d.id);
        if (card) card->setFlash(anyAlarm && d.alarm ? m_flashOn : false);
    }
    m_cardAlarm->setFlash(anyAlarm && m_flashOn);
}

void MainWindow::onDeviceClicked(int deviceId)
{
    if (m_selectedId == deviceId)
        return;

    // 切换选中态高亮
    QMap<int, DeviceCard *>::iterator it = m_cards.begin();
    while (it != m_cards.end()) {
        it.value()->setSelected(it.key() == deviceId);
        ++it;
    }

    // 切换趋势曲线（多设备查看入口）
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).id == deviceId) {
            m_selectedId = deviceId;
            m_chart->showDevice(deviceId, m_devices.at(i).name);
            break;
        }
    }
}

// ===========================================================================
// 告警列表
// ===========================================================================
void MainWindow::pushAlarm(const QString &msg, int level)
{
    const QString now = QTime::currentTime().toString("hh:mm:ss");

    QListWidgetItem *item = new QListWidgetItem;
    item->setText(QString("%1  %2").arg(now).arg(msg));
    item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    // 级别配色：告警红 / 恢复绿 / 警告黄
    switch (level) {
    case 2:  item->setForeground(QColor("#ff5252")); break;
    case 3:  item->setForeground(QColor("#2ecc71")); break;
    default: item->setForeground(QColor("#f1c40f")); break;
    }

    m_alarmList->insertItem(0, item);       // 新告警置顶，滚动列表
    while (m_alarmList->count() > 100)      // 上限 100 条，防内存膨胀
        delete m_alarmList->takeItem(m_alarmList->count() - 1);

    // 告警徽标：统计"告警中"数量
    int active = 0;
    for (int i = 0; i < m_devices.size(); ++i)
        if (m_devices.at(i).alarm) ++active;
    m_alarmBadge->setText(QString::number(active));
    m_alarmBadge->setStyleSheet(active > 0
        ? "background: #e74c3c; color: #ffffff; border-radius: 10px; font-size: 11px; font-weight: bold;"
        : "background: #3a4553; color: #cfd8e3; border-radius: 10px; font-size: 11px; font-weight: bold;");
}
