#include "devicelist.h"
#include "../theme.h"
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QMouseEvent>

DeviceList::DeviceList(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    lay->addStretch();
}

void DeviceList::addDevice(int id, const QString &name)
{
    QFrame *frame = new QFrame(this);
    frame->setFixedHeight(22);
    frame->setCursor(Qt::PointingHandCursor);
    frame->installEventFilter(this);

    QLabel *led = new QLabel(frame);
    led->setFixedSize(8, 8);

    QLabel *nameL = new QLabel(name, frame);
    QFont nf = nameL->font();
    nf.setPixelSize(9);
    nameL->setFont(nf);

    QLabel *temp = new QLabel("--", frame);
    QFont tf = temp->font();
    tf.setPixelSize(10);
    tf.setBold(true);
    temp->setFont(tf);

    QHBoxLayout *hl = new QHBoxLayout(frame);
    hl->setContentsMargins(5, 0, 5, 0);
    hl->setSpacing(4);
    hl->addWidget(led);
    hl->addWidget(nameL);
    hl->addStretch();
    hl->addWidget(temp);

    Row r;
    r.frame = frame;
    r.led = led;
    r.name = nameL;
    r.temp = temp;
    m_rows.insert(id, r);

    // 插入到末尾 stretch 之前, 保持添加顺序
    QVBoxLayout *lay = qobject_cast<QVBoxLayout *>(layout());
    lay->insertWidget(lay->count() - 1, frame, 0, Qt::AlignTop);
    applyRowStyle(id);
}

void DeviceList::updateDevice(const DeviceData &d)
{
    if (!m_rows.contains(d.id))
        return;
    Row &r = m_rows[d.id];
    r.online = d.online;
    r.alarm = d.alarmLevel > 0;
    r.running = d.runStatus != 0;
    r.temp->setText(d.online ? QString::number(d.temp, 'f', 1) + "°C" : "--");
    applyRowStyle(d.id);
}

void DeviceList::applyRowStyle(int id)
{
    Row &r = m_rows[id];
    const bool sel = (id == m_selected);

    QString ledColor;
    if (!r.online)
        ledColor = Theme::Offline.name();
    else if (r.alarm)
        ledColor = m_blink ? "transparent" : Theme::Danger.name();
    else if (r.running)
        ledColor = Theme::Ok.name();
    else
        ledColor = "#5a6b7a";

    r.frame->setStyleSheet(QString(
        "QFrame { background:%1; border:1px solid %2; border-radius:2px; }")
        .arg(sel ? "#1c2833" : "#131c25",
             sel ? Theme::Accent.name() : "#24313d"));
    r.led->setStyleSheet(QString("background:%1; border-radius:4px;").arg(ledColor));
    r.name->setStyleSheet(QString("color:%1;").arg(
        r.online ? Theme::Text.name() : Theme::Dim.name()));
    r.temp->setStyleSheet(QString("color:%1;").arg(
        r.alarm ? Theme::Danger.name()
                : (r.online ? Theme::Text.name() : Theme::Dim.name())));
}

void DeviceList::setSelected(int id)
{
    if (m_selected == id)
        return;
    m_selected = id;
    const QList<int> keys = m_rows.keys();
    for (int k : keys)
        applyRowStyle(k);
}

void DeviceList::setBlink(bool blink)
{
    if (m_blink == blink)
        return;
    m_blink = blink;
    const QList<int> keys = m_rows.keys();
    for (int k : keys)
        applyRowStyle(k);
}

bool DeviceList::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        for (QMap<int, Row>::iterator it = m_rows.begin(); it != m_rows.end(); ++it) {
            if (it.value().frame == obj) {
                setSelected(it.key());
                emit deviceSelected(it.key());
                break;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
