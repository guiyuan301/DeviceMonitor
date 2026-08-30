#include "historypage.h"
#include "../core/datamanager.h"
#include "../widgets/trendchart.h"
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QDateTime>

HistoryPage::HistoryPage(QWidget *parent) : QWidget(parent)
{
    // ---- 查询条件行 ----
    QLabel *l1 = new QLabel("设备");
    m_devCombo = new QComboBox;
    const QList<int> ids = DataManager::instance().deviceIds();
    for (int id : ids)
        m_devCombo->addItem(DataManager::instance().deviceName(id), id);

    QLabel *l2 = new QLabel("最近");
    m_minutes = new QSpinBox;
    m_minutes->setRange(5, 60);
    m_minutes->setValue(15);
    m_minutes->setSuffix(" 分钟");

    m_queryBtn = new QPushButton("查 询");
    m_queryBtn->setObjectName("queryBtn");

    m_info = new QLabel("");
    m_info->setObjectName("panelTitle");

    QHBoxLayout *ctrl = new QHBoxLayout;
    ctrl->setContentsMargins(0, 0, 0, 0);
    ctrl->setSpacing(4);
    ctrl->addWidget(l1);
    ctrl->addWidget(m_devCombo);
    ctrl->addWidget(l2);
    ctrl->addWidget(m_minutes);
    ctrl->addWidget(m_queryBtn);
    ctrl->addStretch();
    ctrl->addWidget(m_info);

    // ---- 曲线 ----
    m_chart = new TrendChart;
    m_chart->setTitle("历史温度曲线 (°C)");
    m_chart->setYRange(20, 90);
    m_chart->setAutoRangeX();
    m_chart->addSeries("温度", QColor(0x00, 0xc8, 0x96));

    // ---- 记录表格 ----
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels(QStringList() << "时间" << "温度(°C)" << "状态" << "产量(件)");
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(16);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    lay->addLayout(ctrl);
    lay->addWidget(m_chart, 1);
    lay->addWidget(m_table, 1);

    connect(m_queryBtn, &QPushButton::clicked, this, &HistoryPage::onQuery);
    if (!ids.isEmpty())
        onQuery();
}

void HistoryPage::showEvent(QShowEvent *)
{
    // 每次切到本页自动按当前条件刷新一次
    onQuery();
}

void HistoryPage::onQuery()
{
    const int id = m_devCombo->currentData().toInt();
    const int minutes = m_minutes->value();
    const QVector<Sample> data = DataManager::instance().recentHistory(id, minutes * 60);

    QVector<QPointF> pts;
    for (int i = 0; i < data.size(); ++i)
        pts.append(QPointF(data.at(i).t, data.at(i).temp));
    m_chart->setSeriesData(0, pts);
    m_chart->setTitle(QString("%1 · 最近 %2 分钟温度曲线")
                      .arg(DataManager::instance().deviceName(id)).arg(minutes));

    // 表格: 最新在前, 最多 60 行
    const int rows = qMin(60, data.size());
    m_table->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        const Sample &s = data[data.size() - 1 - i];
        m_table->setItem(i, 0, new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(s.t).toString("MM-dd hh:mm:ss")));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(s.temp, 'f', 1)));
        m_table->setItem(i, 2, new QTableWidgetItem(s.status ? "运行" : "停机"));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(s.output)));
    }
    m_info->setText(QString("共 %1 条记录").arg(data.size()));
}
