#ifndef DEVICECARD_H
#define DEVICECARD_H

#include <QWidget>
#include <QColor>

/**
 * @brief 设备状态卡片（自绘控件）
 *
 * 用于看板中部的设备网格，每个卡片对应一台车间设备：
 *  - 顶部：状态指示灯 + 设备名称 + 设备号
 *  - 中部：当前温度大字号（超阈值变红）
 *  - 底部：产量 + 状态胶囊文字（运行中/停机/离线/告警）
 *
 * 点击卡片发出 clicked(deviceId)，供主窗口切换趋势曲线（多设备查看入口）。
 */
class DeviceCard : public QWidget
{
    Q_OBJECT
public:
    // 设备状态枚举（与协议中"运行状态"字段对应）
    enum DeviceState {
        StateRunning = 0,   // 运行中（绿灯）
        StateIdle    = 1,   // 停机（黄灯）
        StateOffline = 2,   // 离线（灰灯）
        StateAlarm   = 3    // 告警（红灯，边框闪烁）
    };
    Q_ENUM(DeviceState)

    explicit DeviceCard(QWidget *parent = 0);

    void setDevice(int id, const QString &name);
    void setState(int state);
    void setTemp(double temp);      // 传 < -900 表示离线无数据，显示 "--"
    void setOutput(int count);
    void setSelected(bool selected);
    void setFlash(bool on);         // 告警闪烁开关（由主窗口定时器驱动）
    void setCompact(bool compact);  // 小屏紧凑模式：固定小字号、信息完整排布

    int deviceId() const { return m_id; }

signals:
    void clicked(int deviceId);

protected:
    void paintEvent(QPaintEvent *event);
    void mousePressEvent(QMouseEvent *event);

private:
    QColor stateColor() const;
    QString stateText() const;
    void paintCompact(QPainter &p, const QRectF &rc);   // 小屏专用绘制

    int     m_id;
    QString m_name;
    int     m_state;
    double  m_temp;
    int     m_output;
    bool    m_selected;
    bool    m_flash;
    bool    m_compact;
};

#endif // DEVICECARD_H
