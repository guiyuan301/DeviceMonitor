#include "historypage.h"
#include "../core/datamanager.h"
#include "../core/dbbridge.h"
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
#include <QFileDialog>
#include <QFile>
#include <QTextStream>

HistoryPage::HistoryPage(QWidget *parent) : QWidget(parent)
{
    // ---- 查询条件行 ----
    QLabel *l1 = new QLabel("采集点");
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

    m_exportBtn = new QPushButton("导出CSV");
    m_exportBtn->setObjectName("queryBtn");
    m_exportBtn->setToolTip("把当前查询结果导出为 CSV 文件(Excel可直接打开)");

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
    ctrl->addWidget(m_exportBtn);
    ctrl->addStretch();
    ctrl->addWidget(m_info);

    // ---- 温湿度双曲线 ----
    m_chart = new TrendChart;
    m_chart->setTitle("历史温湿度曲线");
    m_chart->setYRange(0, 100);
    m_chart->setAutoRangeX();
    m_chart->addSeries("温度°C", QColor(0x00, 0xa8, 0xff));
    m_chart->addSeries("湿度%", QColor(0x00, 0xc8, 0x96));

    // ---- 记录表格 ----
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels(QStringList()
        << "时间" << "温度(°C)" << "湿度(%RH)" << "产量(件)");
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
    connect(m_exportBtn, &QPushButton::clicked, this, &HistoryPage::onExport);
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
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // V4: 数据库优先(成员D XS_/db 模块), 库里暂无数据时回退内存环形缓冲
    QVector<Sample> data = DbBridge::instance().queryHistory(
        id, now - qint64(minutes) * 60000, now);
    if (data.isEmpty())
        data = DataManager::instance().recentHistory(id, minutes * 60);
    m_lastData = data;   // 缓存本次结果, 导出CSV时直接复用

    QVector<QPointF> ptsT, ptsH;
    for (int i = 0; i < data.size(); ++i) {
        ptsT.append(QPointF(data.at(i).t, data.at(i).temp));
        ptsH.append(QPointF(data.at(i).t, data.at(i).humi));
    }
    m_chart->setSeriesData(0, ptsT);
    m_chart->setSeriesData(1, ptsH);
    m_chart->setTitle(QString("%1 · 最近 %2 分钟温湿度曲线")
                      .arg(DataManager::instance().deviceName(id)).arg(minutes));

    // 表格: 最新在前, 最多 60 行
    const int rows = qMin(60, data.size());
    m_table->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        const Sample &s = data[data.size() - 1 - i];
        m_table->setItem(i, 0, new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(s.t).toString("MM-dd hh:mm:ss")));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(s.temp, 'f', 1)));
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(s.humi, 'f', 0)));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(s.output)));
    }
    m_info->setText(QString("共 %1 条记录").arg(data.size()));
}

/*
 * onExport — V5 新增: 把当前查询结果导出为 CSV
 *
 * 实现要点(给新手的三个坑):
 *  1. 必须写 UTF-8 BOM(setGenerateByteOrderMark), 否则 Windows 的 Excel
 *     打开中文 CSV 会乱码 —— Excel 默认按 ANSI 编码读无 BOM 的文件;
 *  2. 数据按时间正序导出(m_lastData 是最新在前, 导出时倒序遍历),
 *     这样表格里往下看就是时间递增, 符合查看习惯;
 *  3. 日期时间含逗号风险低, 但字符串字段若可能含逗号/引号, 必须用引号包裹
 *     并转义(CSV 标准转义规则), 本表字段全是数字/时间, 未做包裹。
 */
void HistoryPage::onExport()
{
    if (m_lastData.isEmpty()) {
        m_info->setText("没有可导出的数据, 请先查询");
        return;
    }
    const int id = m_devCombo->currentData().toInt();
    const int minutes = m_minutes->value();

    // 弹出系统保存对话框, 默认文件名"车间A_最近15分钟.csv"
    const QString def = QString("%1_最近%2分钟.csv")
                        .arg(DataManager::instance().deviceName(id)).arg(minutes);
    const QString path = QFileDialog::getSaveFileName(
        this, "导出 CSV", def, "CSV 文件 (*.csv);;所有文件 (*)");
    if (path.isEmpty())
        return;   // 用户点了取消

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_info->setText("导出失败: 无法创建文件");
        return;
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts.setGenerateByteOrderMark(true);          // UTF-8 BOM, 防 Excel 乱码
    ts << "时间,温度(°C),湿度(%RH),产量(件)\n";
    for (int i = m_lastData.size() - 1; i >= 0; --i) {
        const Sample &s = m_lastData.at(i);
        ts << QDateTime::fromMSecsSinceEpoch(s.t).toString("yyyy-MM-dd hh:mm:ss")
           << ',' << QString::number(s.temp, 'f', 1)
           << ',' << QString::number(s.humi, 'f', 0)
           << ',' << s.output << '\n';
    }
    f.close();
    m_info->setText(QString("已导出 %1 条记录").arg(m_lastData.size()));
}
