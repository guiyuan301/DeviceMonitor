#include "devicecard.h"

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

DeviceCard::DeviceCard(QWidget *parent)
    : QWidget(parent)
    , m_id(0)
    , m_state(StateOffline)
    , m_temp(0.0)
    , m_output(0)
    , m_selected(false)
    , m_flash(false)
{
    // 允许鼠标点击 + 跟踪鼠标（后续可做悬停效果）
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(150, 104);
}

void DeviceCard::setDevice(int id, const QString &name)
{
    m_id = id;
    m_name = name;
    update();
}

void DeviceCard::setState(int state)
{
    m_state = state;
    update();
}

void DeviceCard::setTemp(double temp)
{
    m_temp = temp;
    update();
}

void DeviceCard::setOutput(int count)
{
    m_output = count;
    update();
}

void DeviceCard::setSelected(bool selected)
{
    m_selected = selected;
    update();
}

void DeviceCard::setFlash(bool on)
{
    m_flash = on;
    update();
}

QColor DeviceCard::stateColor() const
{
    switch (m_state) {
    case StateRunning: return QColor("#2ecc71");  // 绿：运行
    case StateIdle:    return QColor("#f1c40f");  // 黄：停机
    case StateAlarm:   return QColor("#e74c3c");  // 红：告警
    default:           return QColor("#7f8c8d");  // 灰：离线
    }
}

QString DeviceCard::stateText() const
{
    switch (m_state) {
    case StateRunning: return QString::fromUtf8("运行中");
    case StateIdle:    return QString::fromUtf8("停机");
    case StateAlarm:   return QString::fromUtf8("告警");
    default:           return QString::fromUtf8("离线");
    }
}

void DeviceCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rc = rect().adjusted(1, 1, -2, -2);

    // ---------- 1. 卡片底板 ----------
    // 告警时边框在亮红/暗红之间闪烁，抓眼球；选中时用主题青色高亮
    QColor border("#263445");
    if (m_state == StateAlarm)
        border = m_flash ? QColor("#ff4d4d") : QColor("#8c2f2f");
    else if (m_selected)
        border = QColor("#00c8d7");

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#16202c"));
    p.drawRoundedRect(rc, 8, 8);

    QPen pen(border, m_selected ? 2 : 1);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rc.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    // ---------- 2. 顶部：状态灯 + 名称 + 设备号 ----------
    // 状态灯：外圈微光 + 实心圆
    QColor led = stateColor();
    if (m_state == StateAlarm)
        led = m_flash ? QColor("#ff5252") : QColor("#c0392b");

    QPointF ledCenter(rc.left() + 14, rc.top() + 16);
    QRadialGradient glow(ledCenter, 8);
    glow.setColorAt(0.0, QColor(led.red(), led.green(), led.blue(), 160));
    glow.setColorAt(1.0, QColor(led.red(), led.green(), led.blue(), 0));
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(glow));
    p.drawEllipse(ledCenter, 8, 8);

    p.setBrush(led);
    p.drawEllipse(ledCenter, 4.5, 4.5);

    // 设备名称
    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor("#dfe8f2"));
    QFontMetrics fm(f);
    QString name = fm.elidedText(m_name, Qt::ElideRight, int(rc.width()) - 78);
    p.drawText(QRectF(rc.left() + 26, rc.top() + 6, rc.width() - 60, 18),
               Qt::AlignVCenter | Qt::AlignLeft, name);

    // 设备号（右上角）
    f.setPixelSize(10);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor("#5a6b7d"));
    p.drawText(QRectF(rc.right() - 46, rc.top() + 6, 40, 14),
               Qt::AlignVCenter | Qt::AlignRight, QString("#%1").arg(m_id));

    // ---------- 3. 中部：温度大字号 ----------
    const bool offline = (m_state == StateOffline);
    QString tempText = offline ? QString::fromUtf8("--") : QString::number(m_temp, 'f', 1);
    QColor tempColor = QColor("#e8f0f8");
    if (!offline && m_temp >= 80.0)
        tempColor = QColor("#ff5252");   // 超阈值红色
    else if (!offline && m_temp >= 70.0)
        tempColor = QColor("#ffa726");   // 预警橙色

    f.setPixelSize(24);
    f.setBold(true);
    p.setFont(f);
    p.setPen(tempColor);
    QRectF tempRect(rc.left() + 10, rc.top() + 26, rc.width() - 20, 34);
    p.drawText(tempRect, Qt::AlignVCenter | Qt::AlignLeft, tempText);

    // 温度的 ℃ 小字
    if (!offline) {
        QFontMetrics tfm(f);
        int w = tfm.width(tempText);
        f.setPixelSize(11);
        f.setBold(false);
        p.setFont(f);
        p.setPen(QColor(tempColor.red(), tempColor.green(), tempColor.blue(), 190));
        p.drawText(QRectF(rc.left() + 12 + w, rc.top() + 26, 40, 34),
                   Qt::AlignBottom | Qt::AlignLeft, QString::fromUtf8(" ℃"));
    }

    // ---------- 4. 底部：产量 + 状态胶囊 ----------
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(QColor("#8fa3b8"));
    p.drawText(QRectF(rc.left() + 10, rc.bottom() - 22, rc.width() - 80, 16),
               Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromUtf8("产量  %1").arg(offline ? QString("--") : QString::number(m_output)));

    // 状态胶囊：圆角底 + 状态文字
    QString stText = stateText();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);
    QFontMetrics sfm(f);
    int pillW = sfm.width(stText) + 14;
    QRectF pillRect(rc.right() - 8 - pillW, rc.bottom() - 24, pillW, 16);

    QColor pillBg = QColor(led.red(), led.green(), led.blue(), 38);
    p.setPen(Qt::NoPen);
    p.setBrush(pillBg);
    p.drawRoundedRect(pillRect, 8, 8);

    p.setPen(led);
    p.drawText(pillRect, Qt::AlignCenter, stText);
}

void DeviceCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked(m_id);
    QWidget::mousePressEvent(event);
}
