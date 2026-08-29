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
    , m_compact(false)
{
    // 允许鼠标点击 + 跟踪鼠标（后续可做悬停效果）
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(150, 104);
}

void DeviceCard::setCompact(bool compact)
{
    m_compact = compact;
    update();
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

    // 小屏紧凑模式：专用布局，信息与 PC 端完全一致
    if (m_compact) {
        paintCompact(p, rc);
        return;
    }

    // ---- 自适应缩放：以 150x104 的"标准卡片"为基准，小屏等比缩小 ----
    const double s = qBound(0.45, qMin(rc.width() / 150.0, rc.height() / 104.0), 1.4);
    // 尺寸太小时隐藏次要信息，只保留：状态灯 + 温度 + 设备号
    const bool showName   = rc.width() >= 95;
    const bool showFooter = rc.height() >= 66;

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

    const QPointF ledCenter(rc.left() + 14 * s, rc.top() + 16 * s);
    QRadialGradient glow(ledCenter, 8 * s);
    glow.setColorAt(0.0, QColor(led.red(), led.green(), led.blue(), 160));
    glow.setColorAt(1.0, QColor(led.red(), led.green(), led.blue(), 0));
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(glow));
    p.drawEllipse(ledCenter, 8 * s, 8 * s);

    p.setBrush(led);
    p.drawEllipse(ledCenter, 4.5 * s, 4.5 * s);

    QFont f = font();

    // 设备号（右上角，始终显示）
    f.setPixelSize(qMax(6, int(10 * s)));
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor("#5a6b7d"));
    p.drawText(QRectF(rc.right() - 46 * s, rc.top() + 6 * s, 40 * s, 14 * s),
               Qt::AlignVCenter | Qt::AlignRight, QString("#%1").arg(m_id));

    // 设备名称（小屏空间不够时省略）
    if (showName) {
        f.setPixelSize(qMax(7, int(12 * s)));
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor("#dfe8f2"));
        QFontMetrics fm(f);
        QString name = fm.elidedText(m_name, Qt::ElideRight, int(rc.width()) - 78 * s);
        p.drawText(QRectF(rc.left() + 26 * s, rc.top() + 6 * s,
                          rc.width() - 60 * s, 18 * s),
                   Qt::AlignVCenter | Qt::AlignLeft, name);
    }

    // ---------- 3. 中部：温度大字号 ----------
    const bool offline = (m_state == StateOffline);
    QString tempText = offline ? QString::fromUtf8("--") : QString::number(m_temp, 'f', 1);
    QColor tempColor = QColor("#e8f0f8");
    if (!offline && m_temp >= 80.0)
        tempColor = QColor("#ff5252");   // 超阈值红色
    else if (!offline && m_temp >= 70.0)
        tempColor = QColor("#ffa726");   // 预警橙色

    f.setPixelSize(qMax(10, int(24 * s)));
    f.setBold(true);
    p.setFont(f);
    p.setPen(tempColor);
    const QRectF tempRect(rc.left() + 10 * s, rc.top() + 26 * s,
                          rc.width() - 20 * s, 34 * s);
    p.drawText(tempRect, Qt::AlignVCenter | Qt::AlignLeft, tempText);

    // 温度的 ℃ 小字
    if (!offline) {
        QFontMetrics tfm(f);
        const int w = tfm.width(tempText);
        f.setPixelSize(qMax(6, int(11 * s)));
        f.setBold(false);
        p.setFont(f);
        p.setPen(QColor(tempColor.red(), tempColor.green(), tempColor.blue(), 190));
        p.drawText(QRectF(rc.left() + (12 + w) * s, rc.top() + 26 * s, 40 * s, 34 * s),
                   Qt::AlignBottom | Qt::AlignLeft, QString::fromUtf8(" ℃"));
    }

    // ---------- 4. 底部：产量 + 状态胶囊（小屏省略） ----------
    if (!showFooter)
        return;

    f.setPixelSize(qMax(6, int(10 * s)));
    p.setFont(f);
    p.setPen(QColor("#8fa3b8"));
    p.drawText(QRectF(rc.left() + 10 * s, rc.bottom() - 22 * s,
                      rc.width() - 80 * s, 16 * s),
               Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromUtf8("产量  %1").arg(offline ? QString("--") : QString::number(m_output)));

    // 状态胶囊：圆角底 + 状态文字
    QString stText = stateText();
    f.setPixelSize(qMax(6, int(10 * s)));
    f.setBold(true);
    p.setFont(f);
    QFontMetrics sfm(f);
    const int pillW = sfm.width(stText) + 14 * s;
    const QRectF pillRect(rc.right() - 8 * s - pillW, rc.bottom() - 24 * s,
                          pillW, 16 * s);

    QColor pillBg = QColor(led.red(), led.green(), led.blue(), 38);
    p.setPen(Qt::NoPen);
    p.setBrush(pillBg);
    p.drawRoundedRect(pillRect, 8, 8);

    p.setPen(led);
    p.drawText(pillRect, Qt::AlignCenter, stText);
}

/**
 * @brief 小屏（480x272）专用紧凑绘制
 *
 * 与 PC 端信息完全一致：状态灯 + 名称 + 设备号 + 温度 + 产量 + 状态胶囊，
 * 采用固定小字号 + 密集排布，按约 80x62 的卡片尺寸设计。
 */
void DeviceCard::paintCompact(QPainter &p, const QRectF &rc)
{
    // ---------- 1. 底板 + 边框（告警闪烁 / 选中高亮，与 PC 端一致） ----------
    QColor border("#263445");
    if (m_state == StateAlarm)
        border = m_flash ? QColor("#ff4d4d") : QColor("#8c2f2f");
    else if (m_selected)
        border = QColor("#00c8d7");

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#16202c"));
    p.drawRoundedRect(rc, 6, 6);
    p.setPen(QPen(border, m_selected ? 2 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rc.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

    // ---------- 2. 顶部：状态灯 + 名称 + 设备号 ----------
    QColor led = stateColor();
    if (m_state == StateAlarm)
        led = m_flash ? QColor("#ff5252") : QColor("#c0392b");

    const QPointF ledCenter(rc.left() + 8, rc.top() + 9);
    p.setPen(Qt::NoPen);
    p.setBrush(led);
    p.drawEllipse(ledCenter, 3, 3);

    QFont f = font();

    // 设备名称（能省的像素都省了，但内容不省）
    f.setPixelSize(8);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor("#dfe8f2"));
    QFontMetrics fm(f);
    QString name = fm.elidedText(m_name, Qt::ElideRight, int(rc.width()) - 48);
    p.drawText(QRectF(rc.left() + 14, rc.top() + 3, rc.width() - 44, 11),
               Qt::AlignVCenter | Qt::AlignLeft, name);

    // 设备号（右上角）
    f.setPixelSize(7);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor("#5a6b7d"));
    p.drawText(QRectF(rc.right() - 30, rc.top() + 3, 26, 11),
               Qt::AlignVCenter | Qt::AlignRight, QString("#%1").arg(m_id));

    // ---------- 3. 中部：温度大字 ----------
    const bool offline = (m_state == StateOffline);
    QString tempText = offline ? QString::fromUtf8("--") : QString::number(m_temp, 'f', 1);
    QColor tempColor = QColor("#e8f0f8");
    if (!offline && m_temp >= 80.0)
        tempColor = QColor("#ff5252");
    else if (!offline && m_temp >= 70.0)
        tempColor = QColor("#ffa726");

    const QRectF tempRect(rc.left() + 5, rc.top() + 15, rc.width() - 10, 24);
    f.setPixelSize(15);
    f.setBold(true);
    p.setFont(f);
    p.setPen(tempColor);
    p.drawText(tempRect, Qt::AlignVCenter | Qt::AlignLeft, tempText);

    if (!offline) {
        // 注意：用温度大字自己的字体度量来定位 ℃，避免贴到数字上
        QFontMetrics tfm(f);
        const int tw = tfm.width(tempText);
        f.setPixelSize(8);
        f.setBold(false);
        p.setFont(f);
        p.setPen(QColor(tempColor.red(), tempColor.green(), tempColor.blue(), 190));
        p.drawText(QRectF(rc.left() + 7 + tw, rc.top() + 15, 40, 24),
                   Qt::AlignVCenter | Qt::AlignLeft, QString::fromUtf8("℃"));
    }

    // ---------- 4. 底部：产量 + 状态胶囊 ----------
    f.setPixelSize(7);
    p.setFont(f);
    p.setPen(QColor("#8fa3b8"));
    p.drawText(QRectF(rc.left() + 5, rc.bottom() - 13, 56, 11),
               Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromUtf8("产量 %1").arg(offline ? QString("--")
                                                        : QString::number(m_output)));

    QString stText = stateText();
    f.setPixelSize(7);
    f.setBold(true);
    p.setFont(f);
    QFontMetrics sfm(f);
    const int pillW = sfm.width(stText) + 8;
    const QRectF pillRect(rc.right() - 4 - pillW, rc.bottom() - 14, pillW, 11);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(led.red(), led.green(), led.blue(), 38));
    p.drawRoundedRect(pillRect, 5, 5);

    p.setPen(led);
    p.drawText(pillRect, Qt::AlignCenter, stText);
}

void DeviceCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked(m_id);
    QWidget::mousePressEvent(event);
}
