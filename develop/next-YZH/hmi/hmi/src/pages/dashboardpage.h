#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>
#include "../datatypes.h"

class OverviewCard;
class TrendChart;
class DeviceList;
class AlarmPanel;
class QPushButton;

/*
 * 实时看板页 V2: 顶部总览卡片(在线/均温/均湿/告警),
 * 左设备状态列表, 中温湿度趋势曲线(可切换), 右实时告警+消音。
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
    void onModeTemp();
    void onModeHumi();
    void onMute();

private:
    void refreshCards();
    void loadChartFor(int id);
    void applyMode();

    OverviewCard *m_cardOnline;
    OverviewCard *m_cardTemp;
    OverviewCard *m_cardHumi;
    OverviewCard *m_cardOutput;
    OverviewCard *m_cardAlarm;
    DeviceList *m_devList;
    TrendChart *m_chart;
    AlarmPanel *m_alarmPanel;
    QPushButton *m_btnTemp;
    QPushButton *m_btnHumi;
    QPushButton *m_btnMute;

    int m_selected = 1;
    int m_mode = 0;              // 0=温度 1=湿度
    QVector<QPointF> m_tempPts;  // 选中设备温度序列 (x = epoch ms)
    QVector<QPointF> m_humiPts;  // 选中设备湿度序列
    bool m_blink = false;
};
