#pragma once
#include <QWidget>
#include <QMap>
#include "../datatypes.h"

class QLabel;
class QFrame;

/*
 * 设备状态列表 V2: LED 状态灯 + 点名 + 温度 + 湿度,
 * 点击行选中, 告警设备 LED 闪烁, 离线灰显。
 */
class DeviceList : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceList(QWidget *parent = nullptr);

    void addDevice(int id, const QString &name);
    void updateDevice(const DeviceData &d);
    void setSelected(int id);
    void setBlink(bool blink);

signals:
    void deviceSelected(int id);

protected:
    bool eventFilter(QObject *obj, QEvent *event);

private:
    struct Row {
        QFrame *frame = nullptr;
        QLabel *led = nullptr;
        QLabel *name = nullptr;
        QLabel *temp = nullptr;
        QLabel *humi = nullptr;
        bool online = false;
        bool alarm = false;
    };
    void applyRowStyle(int id);

    QMap<int, Row> m_rows;
    int m_selected = 0;
    bool m_blink = false;
};
