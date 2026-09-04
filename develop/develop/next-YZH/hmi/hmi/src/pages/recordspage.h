#pragma once
#include <QWidget>
#include <QList>
#include "../datatypes.h"

class QTableWidget;
class QLabel;

/*
 * 告警记录页 V2: 全部告警/事件表格 (时间/设备/类型/级别/描述/状态),
 * 右侧事件抓拍预览 —— 点选带抓拍的记录显示摄像头画面。
 */
class RecordsPage : public QWidget
{
    Q_OBJECT
public:
    explicit RecordsPage(QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onRowClicked(int row, int column);

private:
    void showSnapFor(int deviceId);

    QLabel *m_info;
    QTableWidget *m_table;
    QLabel *m_preview;    // 抓拍图
    QLabel *m_prevInfo;   // 抓拍说明
    QList<AlarmItem> m_items;
};
