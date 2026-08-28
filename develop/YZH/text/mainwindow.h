#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QMap>
#include <QTimer>

#include "devicecard.h"
#include "trendchart.h"

class QLabel;
class QListWidget;
class QListWidgetItem;

/**
 * @brief 总览统计卡片（自绘小控件）
 * 顶部一排：设备总数 / 在线 / 运行中 / 告警 / 今日产量 / 平均温度
 */
class StatCard : public QWidget
{
    Q_OBJECT
public:
    explicit StatCard(const QString &title, QWidget *parent = 0);
    void setValueText(const QString &text, const QColor &color);
    void setAccent(const QColor &color);
    void setFlash(bool on);

protected:
    void paintEvent(QPaintEvent *event);

private:
    QString m_title;
    QString m_value;
    QColor  m_valueColor;
    QColor  m_accent;
    bool    m_flash;
};

/**
 * @brief 车间设备监控系统 - 中央板 Qt 大屏看板（阶段1 UI 原型 / 阶段2 模拟数据版）
 *
 * 布局（对应实施文档成员C的看板原型要求）：
 *   ┌────────────────────────────────────────────┐
 *   │ 顶栏：标题 + 服务器连接状态 + 系统时间        │
 *   ├────────────────────────────────────────────┤
 *   │ 总览卡片：总数/在线/运行/告警/产量/平均温度   │
 *   ├───────────────────────────────┬────────────┤
 *   │ 设备状态卡片网格（8台，4x2）    │ 实时告警列表 │
 *   ├───────────────────────────────┴────────────┤
 *   │ 温度趋势曲线（点击设备卡片切换）              │
 *   └────────────────────────────────────────────┘
 *
 * 数据源说明：当前用 QTimer 每秒生成模拟数据（阶段2 的"Qt 界面接收模拟数据"）。
 * 后续接入成员 A 的真实数据时，只需把 onTick() 替换为
 * 信号槽 / QMetaObject::invokeMethod 从共享数据区取最新值即可，界面代码不动。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);

private slots:
    void onTick();          // 1 秒模拟数据驱动
    void onFlash();         // 500ms 告警闪烁驱动
    void onDeviceClicked(int deviceId);

private:
    // ---------- 内部模拟数据结构（后续被真实数据源替换） ----------
    struct DeviceSim {
        int     id;         // 设备号（协议 1~65535）
        QString name;       // 设备名称
        double  temp;       // 当前温度 ℃
        double  baseTemp;   // 温度基准值（随机行走围绕它波动）
        int     state;      // DeviceCard::DeviceState
        int     output;     // 今日产量
        bool    alarm;      // 是否处于告警中（带迟滞，防止阈值附近抖动）
    };

    void buildUi();
    void applyGlobalStyle();
    void simulate();
    void refreshSummary();
    void pushAlarm(const QString &msg, int level);   // level: 0提示 1警告 2告警 3恢复

    // ---------- UI ----------
    QVector<DeviceSim>  m_devices;
    QMap<int, DeviceCard *> m_cards;    // 设备号 -> 卡片
    StatCard *m_cardTotal;
    StatCard *m_cardOnline;
    StatCard *m_cardRun;
    StatCard *m_cardAlarm;
    StatCard *m_cardOutput;
    StatCard *m_cardTemp;
    TrendChart *m_chart;
    QListWidget *m_alarmList;
    QLabel *m_timeLabel;
    QLabel *m_linkLabel;
    QLabel *m_alarmBadge;
    int     m_selectedId;

    // ---------- 定时器 ----------
    QTimer m_tickTimer;     // 数据周期（对齐协议 1s 上报节奏）
    QTimer m_flashTimer;    // 告警闪烁周期
    bool   m_flashOn;
};

#endif // MAINWINDOW_H
