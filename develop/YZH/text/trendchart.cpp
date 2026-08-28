#include "trendchart.h"

#include <QPainter>
#include <QFontMetrics>

TrendChart::TrendChart(QWidget *parent)
    : QWidget(parent)
    , m_deviceId(-1)
    , m_threshold(80.0)
    , m_yMin(20.0)
    , m_yMax(100.0)
    , m_maxPoints(120)
{
    setMinimumHeight(150);
}

void TrendChart::addSample(int deviceId, double value)
{
    QVector<double> &s = m_series[deviceId];
    s.append(value);
    while (s.size() > m_maxPoints)
        s.remove(0);           // 环形缓冲：超过窗口长度就从头部丢
    if (deviceId == m_deviceId)
        update();
}

void TrendChart::showDevice(int deviceId, const QString &name)
{
    m_deviceId = deviceId;
    m_name = name;
    update();
}

void TrendChart::setThreshold(double value)
{
    m_threshold = value;
    update();
}

void TrendChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 整体底板
    QRectF panel = rect().adjusted(1, 1, -2, -2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#101822"));
    p.drawRoundedRect(panel, 8, 8);
    p.setPen(QPen(QColor("#263445"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    // 绘图区（留出坐标轴和标题空间）
    QRectF plot = panel.adjusted(44, 34, -14, -24);
    drawFrame(p, plot);
    drawGrid(p, plot);
    drawThreshold(p, plot);
    drawSeries(p, plot);
    drawHeader(p);
}

void TrendChart::drawFrame(QPainter &p, const QRectF &plot)
{
    p.setPen(QPen(QColor("#2c3d50"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
}

void TrendChart::drawGrid(QPainter &p, const QRectF &plot)
{
    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(QColor("#5a6b7d"));

    // 横向网格：从 yMin 到 yMax 画 5 条，右侧标注刻度值
    const int lines = 4;
    for (int i = 0; i <= lines; ++i) {
        double v = m_yMin + (m_yMax - m_yMin) * i / lines;
        double y = plot.bottom() - plot.height() * i / lines;
        if (i > 0) {
            QPen pen(QColor("#1d2a38"), 1, Qt::DotLine);
            p.setPen(pen);
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        p.setPen(QColor("#5a6b7d"));
        p.drawText(QRectF(plot.right() + 2, y - 7, 30, 14),
                   Qt::AlignVCenter | Qt::AlignLeft, QString::number(v, 'f', 0));
    }

    // X 轴时间提示：曲线窗口约 120 秒（1 秒 1 个采样点）
    p.drawText(QRectF(plot.left(), plot.bottom() + 3, 60, 14),
               Qt::AlignLeft, QString::fromUtf8("-2min"));
    p.drawText(QRectF(plot.center().x() - 30, plot.bottom() + 3, 60, 14),
               Qt::AlignCenter, QString::fromUtf8("-1min"));
    p.drawText(QRectF(plot.right() - 60, plot.bottom() + 3, 60, 14),
               Qt::AlignRight, QString::fromUtf8("现在"));
}

void TrendChart::drawThreshold(QPainter &p, const QRectF &plot)
{
    if (m_threshold <= m_yMin || m_threshold >= m_yMax)
        return;
    double y = plot.bottom() - plot.height() * (m_threshold - m_yMin) / (m_yMax - m_yMin);
    QPen pen(QColor("#e74c3c"), 1, Qt::DashLine);
    p.setPen(pen);
    p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));

    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(QColor("#e74c3c"));
    p.drawText(QRectF(plot.left() + 4, y - 14, 90, 12),
               Qt::AlignLeft | Qt::AlignBottom,
               QString::fromUtf8("阈值 %1℃").arg(m_threshold, 0, 'f', 0));
}

void TrendChart::drawSeries(QPainter &p, const QRectF &plot)
{
    if (m_deviceId < 0 || !m_series.contains(m_deviceId))
        return;
    const QVector<double> &s = m_series.value(m_deviceId);
    if (s.isEmpty())
        return;

    // 数值 -> 像素坐标
    QVector<QPointF> pts;
    pts.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        double x = plot.left() + plot.width() * i / (m_maxPoints - 1);
        double v = qBound(m_yMin, s.at(i), m_yMax);
        double y = plot.bottom() - plot.height() * (v - m_yMin) / (m_yMax - m_yMin);
        pts.append(QPointF(x, y));
    }

    // 曲线下方渐变填充，增加"大屏质感"
    QPointF last = pts.last();
    QLinearGradient grad(0, plot.top(), 0, plot.bottom());
    grad.setColorAt(0.0, QColor(0, 200, 215, 70));
    grad.setColorAt(1.0, QColor(0, 200, 215, 0));
    QPolygonF fillPoly;
    fillPoly << QPointF(pts.first().x(), plot.bottom());
    for (int i = 0; i < pts.size(); ++i)
        fillPoly << pts.at(i);
    fillPoly << QPointF(last.x(), plot.bottom());
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(grad));
    p.drawPolygon(fillPoly);

    // 曲线本体
    QPen pen(QColor("#00c8d7"), 2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(pts.data(), pts.size());

    // 末点圆点
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#00c8d7"));
    p.drawEllipse(last, 3.5, 3.5);
    p.setPen(QPen(QColor("#101822"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(last, 3.5, 3.5);
}

void TrendChart::drawHeader(QPainter &p)
{
    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);

    QRectF head = rect().adjusted(12, 8, -12, 0);
    head.setHeight(20);

    if (m_deviceId < 0) {
        p.setPen(QColor("#5a6b7d"));
        p.drawText(head, Qt::AlignVCenter | Qt::AlignLeft, QString::fromUtf8("温度趋势"));
        return;
    }

    QString title = QString::fromUtf8("温度趋势 · %1").arg(m_name);
    p.setPen(QColor("#dfe8f2"));
    p.drawText(QRectF(head.left(), head.top(), head.width() * 0.6, head.height()),
               Qt::AlignVCenter | Qt::AlignLeft, title);

    // 右侧：当前值 + 单位（超阈值红色提醒）
    const QVector<double> &s = m_series.value(m_deviceId);
    if (!s.isEmpty()) {
        double v = s.last();
        QColor c = (v >= m_threshold) ? QColor("#ff5252") : QColor("#00c8d7");
        f.setPixelSize(14);
        f.setBold(true);
        p.setFont(f);
        QString val = QString::number(v, 'f', 1) + QString::fromUtf8(" ℃");
        p.setPen(c);
        p.drawText(head, Qt::AlignVCenter | Qt::AlignRight, val);
    }
}
