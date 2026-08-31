#pragma once
#include <QFrame>

class QLabel;

// 顶部总览指标卡片: 标题 + 大数值 + 单位 + 副标题, 告警时红色描边
class OverviewCard : public QFrame
{
    Q_OBJECT
public:
    explicit OverviewCard(const QString &title, const QString &unit,
                          QWidget *parent = nullptr);

    void setValue(const QString &v);
    void setSub(const QString &s);
    void setAlarm(bool on);

private:
    QLabel *m_value;
    QLabel *m_sub;
    bool m_alarm;
};
