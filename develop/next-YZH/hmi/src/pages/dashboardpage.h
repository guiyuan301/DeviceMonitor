#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>
#include "../datatypes.h"

class OverviewCard;
class TrendChart;
class DeviceList;
class AlarmPanel;

/*
 * 实时看板页: 顶部总览卡片(在线/均温/产量/告警),
 * 左设备状态列表, 中趋势曲线, 右实时告警。
 */
class DashboardPage : public QWidget
{
    Q_OBJECT
public:
    explicit DashboardPage(QWidget *parent = nullptr);

private slots:
    void onDeviceUpdated(int id);
    void onAlarmListChanged();
    void onDeviceSelected(int id);
    void onBlink();

private:
    void refreshCards();
    void loadChartFor(int id);

    OverviewCard *m_cardOnline;
    OverviewCard *m_cardTemp;
    OverviewCard *m_cardOutput;
    OverviewCard *m_cardAlarm;
    DeviceList *m_devList;
    TrendChart *m_chart;
    AlarmPanel *m_alarmPanel;

    int m_selected = 1;
    QVector<QPointF> m_points;  // 选中设备温度序列 (x = epoch ms)
    bool m_blink = false;
};
