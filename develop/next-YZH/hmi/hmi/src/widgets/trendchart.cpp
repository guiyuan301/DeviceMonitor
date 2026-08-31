#include "trendchart.h"
#include "../theme.h"
#include <QPainter>
#include <QDateTime>
#include <QFontMetrics>

TrendChart::TrendChart(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(120, 80);
}

void TrendChart::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void TrendChart::setYRange(double min, double max)
{
    m_ymin = min;
    m_ymax = max;
    update();
}

void TrendChart::setSlidingWindow(int seconds)
{
    m_autoX = false;
    m_xwin = seconds;
    update();
}

void TrendChart::setAutoRangeX()
{
    m_autoX = true;
    update();
}

int TrendChart::addSeries(const QString &name, const QColor &color)
{
    Series s;
    s.name = name;
    s.color = color;
    m_series.append(s);
    update();
    return m_series.size() - 1;
}

void TrendChart::setSeriesData(int index, const QVector<QPointF> &points)
{
    if (index < 0 || index >= m_series.size())
        return;
    m_series[index].points = points;
    update();
}

void TrendChart::clearSeries()
{
    for (int i = 0; i < m_series.size(); ++i)
        m_series[i].points.clear();
    update();
}

void TrendChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRect area = rect().adjusted(1, 1, -1, -1);
    p.setBrush(QColor(0x12, 0x19, 0x20));
    p.setPen(QPen(Theme::PanelLine, 1));
    p.drawRect(area);
    p.setBrush(Qt::NoBrush);

    QFont f8 = font(); f8.setPixelSize(8);
    QFont f9 = font(); f9.setPixelSize(9);

    // ---- x 轴时间范围 ----
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 tMin = 0, tMax = 0;
    if (m_autoX) {
        bool found = false;
        for (int i = 0; i < m_series.size(); ++i) {
            const QVector<QPointF> &pts = m_series.at(i).points;
            for (int k = 0; k < pts.size(); ++k) {
                const qint64 t = qint64(pts.at(k).x());
                if (!found) { tMin = t; tMax = t; found = true; }
                else { if (t < tMin) tMin = t; if (t > tMax) tMax = t; }
            }
        }
        if (!found) { tMin = now - 60000; tMax = now; }
        if (tMax - tMin < 1000)
            tMax = tMin + 1000;
    } else {
        tMax = now;
        tMin = now - qint64(m_xwin) * 1000;
    }
    const double xspan = double(tMax - tMin);

    // ---- 绘图区 ----
    const QRect plot(area.left() + 30, area.top() + 16,
                     area.width() - 36, area.height() - 30);

    // y 网格与刻度
    p.setFont(f8);
    for (int i = 0; i <= 4; ++i) {
        const double v = m_ymin + (m_ymax - m_ymin) * i / 4.0;
        const int y = int(plot.bottom() - plot.height() * i / 4.0);
        p.setPen(QPen(Theme::Grid, 1));
        p.drawLine(plot.left(), y, plot.right(), y);
        p.setPen(Theme::Dim);
        p.drawText(QRect(area.left(), y - 6, plot.left() - area.left() - 3, 12),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', 0));
    }

    // x 刻度
    p.setPen(Theme::Dim);
    p.setFont(f8);
    if (!m_autoX) {
        p.drawText(QRect(plot.left() - 20, plot.bottom() + 2, 50, 10),
                   Qt::AlignLeft, QString("-%1s").arg(m_xwin));
        p.drawText(QRect(plot.center().x() - 20, plot.bottom() + 2, 40, 10),
                   Qt::AlignCenter, QString("-%1s").arg(m_xwin / 2));
        p.drawText(QRect(plot.right() - 40, plot.bottom() + 2, 40, 10),
                   Qt::AlignRight, "now");
    } else {
        p.drawText(QRect(plot.left() - 20, plot.bottom() + 2, 72, 10), Qt::AlignLeft,
                   QDateTime::fromMSecsSinceEpoch(tMin).toString("hh:mm:ss"));
        p.drawText(QRect(plot.right() - 72, plot.bottom() + 2, 72, 10), Qt::AlignRight,
                   QDateTime::fromMSecsSinceEpoch(tMax).toString("hh:mm:ss"));
    }

    // 标题与图例
    p.setFont(f9);
    p.setPen(Theme::Text);
    p.drawText(QRect(area.left() + 5, area.top() + 2, area.width() - 10, 12),
               Qt::AlignLeft | Qt::AlignVCenter, m_title);

    p.setFont(f8);
    int lx = area.right() - 4;
    for (int i = m_series.size() - 1; i >= 0; --i) {
        const Series &s = m_series.at(i);
        if (s.name.isEmpty())
            continue;
        const int tw = QFontMetrics(f8).width(s.name);
        p.setPen(Theme::Dim);
        p.drawText(QRect(lx - tw, area.top() + 2, tw, 12),
                   Qt::AlignRight | Qt::AlignVCenter, s.name);
        lx -= tw + 3;
        p.setPen(Qt::NoPen);
        p.setBrush(s.color);
        p.drawRect(lx - 6, area.top() + 6, 6, 3);
        lx -= 10;
    }

    // ---- 曲线 ----
    bool anyData = false;
    for (int si = 0; si < m_series.size(); ++si) {
        const Series &s = m_series.at(si);
        // 过滤窗口内可见点
        QVector<QPointF> vis;
        for (int k = 0; k < s.points.size(); ++k) {
            const QPointF &pt = s.points.at(k);
            if (qint64(pt.x()) >= tMin - 1000)
                vis.append(pt);
        }
        if (vis.isEmpty())
            continue;
        anyData = true;

        auto xFor = [&](qint64 t) {
            return plot.left() + (t - tMin) / xspan * plot.width();
        };
        auto yFor = [&](double v) {
            double fr = (v - m_ymin) / (m_ymax - m_ymin);
            fr = qBound(0.0, fr, 1.0);
            return plot.bottom() - fr * plot.height();
        };

        QPainterPath path;
        for (int k = 0; k < vis.size(); ++k) {
            const double x = xFor(qint64(vis.at(k).x()));
            const double y = yFor(vis.at(k).y());
            if (k == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        const double lastX = xFor(qint64(vis.last().x()));
        const double lastY = yFor(vis.last().y());

        if (vis.size() >= 2) {
            // 渐变填充
            QPainterPath fill = path;
            fill.lineTo(lastX, plot.bottom());
            fill.lineTo(path.elementAt(0).x, plot.bottom());
            QLinearGradient g(0, plot.top(), 0, plot.bottom());
            QColor c1 = s.color; c1.setAlpha(60);
            QColor c0 = s.color; c0.setAlpha(0);
            g.setColorAt(0.0, c1);
            g.setColorAt(1.0, c0);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawPath(fill);

            p.setPen(QPen(s.color, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }

        // 末端数值气泡
        QString vs = QString::number(vis.last().y(), 'f', 1);
        const int vw = QFontMetrics(f8).width(vs) + 6;
        QRect vbr(int(lastX) - vw / 2, int(lastY) - 14, vw, 11);
        if (vbr.left() < plot.left()) vbr.moveLeft(plot.left());
        if (vbr.right() > plot.right()) vbr.moveRight(plot.right());
        if (vbr.top() < plot.top()) vbr.moveTop(int(lastY) + 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(vbr, 2, 2);
        p.setPen(s.color);
        p.setFont(f8);
        p.drawText(vbr, Qt::AlignCenter, vs);

        p.setBrush(s.color);
        p.setPen(QPen(Qt::white, 1));
        p.drawEllipse(QPointF(lastX, lastY), 2.5, 2.5);
    }

    if (!anyData) {
        p.setFont(f9);
        p.setPen(Theme::Dim);
        p.drawText(plot, Qt::AlignCenter, "暂无数据");
    }
}
