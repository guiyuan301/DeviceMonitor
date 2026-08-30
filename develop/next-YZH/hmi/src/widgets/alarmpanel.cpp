#include "alarmpanel.h"
#include "../theme.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QDateTime>

AlarmPanel::AlarmPanel(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QWidget *host = new QWidget;
    m_layout = new QVBoxLayout(host);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);
    m_layout->addStretch();

    QScrollArea *sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setWidget(host);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(sa);
}

QWidget *AlarmPanel::makeRow(const AlarmItem &item)
{
    QString lvlName;
    QColor c;
    if (item.active) {
        if (item.level >= 2) { lvlName = "告警"; c = Theme::Danger; }
        else                 { lvlName = "预警"; c = Theme::Warn; }
    } else {
        lvlName = "恢复";
        c = Theme::Ok;
    }

    const QString t = QDateTime::fromMSecsSinceEpoch(item.raised).toString("MM-dd hh:mm:ss");
    QLabel *lb = new QLabel(QString(
        "<b style='color:%1'>[%2]</b> <span style='color:#d8e2ea;'>%3</span><br>"
        "<span style='color:#7c8d9c;'>%4</span>")
        .arg(c.name(), lvlName, item.deviceName + " " + item.message, t));
    lb->setWordWrap(true);
    lb->setStyleSheet(QString(
        "background:#131c25; border-left:2px solid %1; padding:2px 4px;").arg(c.name()));
    return lb;
}

void AlarmPanel::setAlarms(const QList<AlarmItem> &items)
{
    while (QLayoutItem *it = m_layout->takeAt(0)) {
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    for (int i = 0; i < items.size(); ++i)
        m_layout->addWidget(makeRow(items.at(i)), 0, Qt::AlignTop);
    m_layout->addStretch();
}
