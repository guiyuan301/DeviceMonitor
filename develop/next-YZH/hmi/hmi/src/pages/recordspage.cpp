#include "recordspage.h"
#include "../core/datamanager.h"
#include "../theme.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QDateTime>
#include <QPixmap>
#include <QColor>

RecordsPage::RecordsPage(QWidget *parent) : QWidget(parent)
{
    m_info = new QLabel("");
    m_info->setObjectName("panelTitle");

    m_table = new QTableWidget(0, 6);
    m_table->setHorizontalHeaderLabels(QStringList()
        << "触发时间" << "设备" << "类型" << "级别" << "描述" << "状态");
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(16);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);

    // ---- 右侧抓拍预览 ----
    QFrame *snapPanel = new QFrame;
    snapPanel->setObjectName("panel");
    snapPanel->setFixedWidth(150);
    QVBoxLayout *sl = new QVBoxLayout(snapPanel);
    sl->setContentsMargins(3, 3, 3, 3);
    sl->setSpacing(3);
    QLabel *snapTitle = new QLabel("事件抓拍");
    snapTitle->setObjectName("panelTitle");
    m_preview = new QLabel;
    m_preview->setObjectName("snapThumb");
    m_preview->setFixedSize(140, 100);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setText("点选左侧记录");
    m_prevInfo = new QLabel("");
    m_prevInfo->setObjectName("panelTitle");
    m_prevInfo->setWordWrap(true);
    sl->addWidget(snapTitle);
    sl->addWidget(m_preview);
    sl->addWidget(m_prevInfo, 1);

    QHBoxLayout *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);
    body->addWidget(m_table, 1);
    body->addWidget(snapPanel);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    lay->addWidget(m_info);
    lay->addLayout(body, 1);

    connect(&DataManager::instance(), &DataManager::alarmListChanged,
            this, &RecordsPage::refresh);
    connect(m_table, &QTableWidget::cellClicked, this, &RecordsPage::onRowClicked);
    refresh();
}

void RecordsPage::refresh()
{
    m_items = DataManager::instance().alarms();
    m_table->setRowCount(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        const AlarmItem &a = m_items.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(a.raised).toString("MM-dd hh:mm:ss")));
        m_table->setItem(i, 1, new QTableWidgetItem(a.deviceName));

        QTableWidgetItem *type = new QTableWidgetItem(alarmTypeName(a.type));
        QColor tc = Theme::Text;
        if (a.type == SnapEvent) tc = QColor(0x00, 0xc8, 0x96);
        else if (a.type == OfflineAlarm) tc = QColor(0x7c, 0x8d, 0x9c);
        type->setForeground(tc);
        m_table->setItem(i, 2, type);

        if (a.type == SnapEvent) {
            // 抓拍事件: 仅记录, 无级别/告警状态
            QTableWidgetItem *lvl = new QTableWidgetItem("事件");
            lvl->setForeground(QColor(0x7c, 0x8d, 0x9c));
            m_table->setItem(i, 3, lvl);
            QString desc = a.message;
            if (a.hasSnap)
                desc += "  [抓拍]";
            m_table->setItem(i, 4, new QTableWidgetItem(desc));
            QTableWidgetItem *st = new QTableWidgetItem("记录");
            st->setForeground(QColor(0x2e, 0xcc, 0x71));
            m_table->setItem(i, 5, st);
            continue;
        }

        QTableWidgetItem *lvl = new QTableWidgetItem(a.level >= 2 ? "告警" : "预警");
        lvl->setForeground(a.level >= 2 ? QColor(0xe7, 0x4c, 0x3c)
                                        : QColor(0xf3, 0x9c, 0x12));
        m_table->setItem(i, 3, lvl);

        QString desc = a.message;
        if (a.hasSnap)
            desc += "  [抓拍]";
        m_table->setItem(i, 4, new QTableWidgetItem(desc));

        QTableWidgetItem *st = new QTableWidgetItem(a.active ? "告警中" : "已恢复");
        st->setForeground(a.active ? QColor(0xe7, 0x4c, 0x3c)
                                   : QColor(0x2e, 0xcc, 0x71));
        m_table->setItem(i, 5, st);
    }
    m_info->setText(QString("共 %1 条记录, 其中 %2 条告警中")
                    .arg(m_items.size()).arg(DataManager::instance().activeAlarmCount()));
}

void RecordsPage::onRowClicked(int row, int /*column*/)
{
    if (row < 0 || row >= m_items.size())
        return;
    const AlarmItem &a = m_items.at(row);
    if (!a.hasSnap) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText("该记录无抓拍");
        m_prevInfo->setText("");
        return;
    }
    showSnapFor(a.deviceId);
}

void RecordsPage::showSnapFor(int deviceId)
{
    SnapItem s = DataManager::instance().lastSnapshot(deviceId);
    if (s.path.isEmpty())
        return;
    QPixmap pm(s.path);
    if (!pm.isNull())
        m_preview->setPixmap(pm.scaled(140, 100, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
    m_prevInfo->setText(QString("%1\n%2")
        .arg(DataManager::instance().deviceName(s.deviceId),
             QDateTime::fromMSecsSinceEpoch(s.t).toString("MM-dd hh:mm:ss")));
}
