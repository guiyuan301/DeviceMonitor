#pragma once
#include <QWidget>
#include <QList>
#include "../datatypes.h"

class QVBoxLayout;

// 实时告警滚动列表: 每条两行(级别+设备+描述 / 时间), 新告警置顶
class AlarmPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AlarmPanel(QWidget *parent = nullptr);
    void setAlarms(const QList<AlarmItem> &items);

private:
    QWidget *makeRow(const AlarmItem &item);
    QVBoxLayout *m_layout;
};
