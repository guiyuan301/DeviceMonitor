#pragma once
#include <QWidget>
#include <QMap>
#include "../datatypes.h"

class QLabel;
class QFrame;

/*
 * 设备状态列表: LED 状态灯 + 设备名 + 实时温度, 点击行选中设备,
 * 告警设备的 LED 随 setBlink() 闪烁。
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
        bool online = false;
        bool alarm = false;
        bool running = false;
    };
    void applyRowStyle(int id);

    QMap<int, Row> m_rows;
    int m_selected = 0;
    bool m_blink = false;
};
