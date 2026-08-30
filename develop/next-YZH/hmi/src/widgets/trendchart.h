#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QString>
#include <QColor>

/*
 * 轻量趋势曲线控件 (纯 QPainter, 不依赖 QtCharts, 适合嵌入式):
 *  - 滑动窗口模式: 显示最近 N 秒 (实时看板)
 *  - 自适应范围模式: 按 数据时间范围 缩放 (历史回查)
 *  - 支持多条曲线、渐变填充、末端数值气泡
 */
class TrendChart : public QWidget
{
    Q_OBJECT
public:
    explicit TrendChart(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setYRange(double min, double max);
    void setSlidingWindow(int seconds);   // x 轴 = 最近 N 秒
    void setAutoRangeX();                 // x 轴 = 数据时间范围
    int  addSeries(const QString &name, const QColor &color);
    void setSeriesData(int index, const QVector<QPointF> &points); // x=epoch ms
    void clearSeries();

protected:
    void paintEvent(QPaintEvent *event);

private:
    struct Series {
        QString name;
        QColor color;
        QVector<QPointF> points;
    };

    QVector<Series> m_series;
    QString m_title;
    double m_ymin = 0.0;
    double m_ymax = 100.0;
    int m_xwin = 120;      // 滑动窗口秒数
    bool m_autoX = false;  // 是否按数据自适应 x 范围
};
