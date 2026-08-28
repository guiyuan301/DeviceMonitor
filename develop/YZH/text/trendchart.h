#ifndef TRENDCHART_H
#define TRENDCHART_H

#include <QWidget>
#include <QMap>
#include <QVector>

/**
 * @brief 温度趋势曲线控件（QPainter 自绘，不依赖 QtCharts）
 *
 * 文档要求：趋势曲线绘制在 480x272 小屏上要注意布局，因此：
 *  - 只画一条"当前选中设备"的曲线，避免多线遮挡；
 *  - 数据环形缓冲，最多保留 120 个点（模拟 2 分钟窗口）；
 *  - 自带网格、Y 轴刻度、告警阈值虚线、渐变填充和末值标注。
 *
 * 所有设备的数据都缓存在这里，点击设备卡片调用 showDevice() 即可切换。
 */
class TrendChart : public QWidget
{
    Q_OBJECT
public:
    explicit TrendChart(QWidget *parent = 0);

    // 追加一个采样点（离线设备不要调用，避免断线后画直线）
    void addSample(int deviceId, double value);

    // 切换当前显示的设备（切换后立即重绘）
    void showDevice(int deviceId, const QString &name);

    // 设置告警阈值线（与告警引擎规则联动）
    void setThreshold(double value);

protected:
    void paintEvent(QPaintEvent *event);

private:
    void drawFrame(QPainter &p, const QRectF &plot);
    void drawGrid(QPainter &p, const QRectF &plot);
    void drawThreshold(QPainter &p, const QRectF &plot);
    void drawSeries(QPainter &p, const QRectF &plot);
    void drawHeader(QPainter &p);

    QMap<int, QVector<double> > m_series;   // 每台设备的历史温度
    int     m_deviceId;                     // 当前显示的设备号
    QString m_name;                         // 当前显示的设备名
    double  m_threshold;                    // 告警阈值线
    double  m_yMin;
    double  m_yMax;
    int     m_maxPoints;                    // 环形缓冲长度
};

#endif // TRENDCHART_H
