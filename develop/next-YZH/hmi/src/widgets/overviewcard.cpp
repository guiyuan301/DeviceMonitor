#include "overviewcard.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QVariant>

OverviewCard::OverviewCard(const QString &title, const QString &unit, QWidget *parent)
    : QFrame(parent), m_alarm(false)
{
    setObjectName("overviewCard");
    setFixedHeight(52);

    QLabel *titleL = new QLabel(title, this);
    titleL->setObjectName("cardTitle");

    m_value = new QLabel("--", this);
    m_value->setObjectName("cardValue");

    QLabel *unitL = new QLabel(unit, this);
    unitL->setObjectName("cardUnit");

    m_sub = new QLabel(QString(), this);
    m_sub->setObjectName("cardSub");

    QHBoxLayout *vrow = new QHBoxLayout;
    vrow->setContentsMargins(0, 0, 0, 0);
    vrow->setSpacing(2);
    vrow->addWidget(m_value);
    vrow->addWidget(unitL);
    vrow->addStretch();

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 3, 6, 3);
    lay->setSpacing(0);
    lay->addWidget(titleL);
    lay->addLayout(vrow);
    lay->addWidget(m_sub);
}

void OverviewCard::setValue(const QString &v)
{
    m_value->setText(v);
}

void OverviewCard::setSub(const QString &s)
{
    m_sub->setText(s);
}

void OverviewCard::setAlarm(bool on)
{
    if (on == m_alarm)
        return;
    m_alarm = on;
    setProperty("alarm", on);
    style()->unpolish(this);
    style()->polish(this);
}
