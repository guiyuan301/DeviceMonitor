#include "recordspage.h"
#include "../core/datamanager.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QDateTime>
#include <QColor>

RecordsPage::RecordsPage(QWidget *parent) : QWidget(parent)
{
    m_info = new QLabel("");
    m_info->setObjectName("panelTitle");

    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels(QStringList()
        << "触发时间" << "设备" << "级别" << "描述" << "状态");
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(16);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    lay->addWidget(m_info);
    lay->addWidget(m_table, 1);

    connect(&DataManager::instance(), &DataManager::alarmListChanged,
            this, &RecordsPage::refresh);
    refresh();
}

void RecordsPage::refresh()
{
    const QList<AlarmItem> items = DataManager::instance().alarms();
    m_table->setRowCount(items.size());
    for (int i = 0; i < items.size(); ++i) {
        const AlarmItem &a = items.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(a.raised).toString("MM-dd hh:mm:ss")));
        m_table->setItem(i, 1, new QTableWidgetItem(a.deviceName));
        QTableWidgetItem *lvl = new QTableWidgetItem(a.level >= 2 ? "告警" : "预警");
        lvl->setForeground(a.level >= 2 ? QColor(0xe7, 0x4c, 0x3c)
                                        : QColor(0xf3, 0x9c, 0x12));
        m_table->setItem(i, 2, lvl);
        m_table->setItem(i, 3, new QTableWidgetItem(a.message));
        QTableWidgetItem *st = new QTableWidgetItem(a.active ? "告警中" : "已恢复");
        st->setForeground(a.active ? QColor(0xe7, 0x4c, 0x3c)
                                   : QColor(0x2e, 0xcc, 0x71));
        m_table->setItem(i, 4, st);
    }
    m_info->setText(QString("共 %1 条记录, 其中 %2 条告警中")
                    .arg(items.size()).arg(DataManager::instance().activeAlarmCount()));
}
