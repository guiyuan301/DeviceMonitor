#pragma once
#include <QWidget>

class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;
class TrendChart;
class QTableWidget;

/*
 * 历史回查页 V2: 选采集点 + 时间范围 → 温湿度双曲线回放 + 记录表格。
 * 当前数据来自 DataManager 内存环形缓冲; 接 SQLite 后换用
 * 历史查询接口即可, 界面不变。
 */
class HistoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPage(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event);

private slots:
    void onQuery();

private:
    QComboBox *m_devCombo;
    QSpinBox *m_minutes;
    QPushButton *m_queryBtn;
    QLabel *m_info;
    TrendChart *m_chart;
    QTableWidget *m_table;
};
