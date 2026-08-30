#pragma once
#include <QWidget>

class QTableWidget;
class QLabel;

// 告警记录页: 全部历史告警表格 (触发时间/设备/级别/描述/状态)
class RecordsPage : public QWidget
{
    Q_OBJECT
public:
    explicit RecordsPage(QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    QLabel *m_info;
    QTableWidget *m_table;
};
