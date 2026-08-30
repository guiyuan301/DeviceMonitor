#include "videopage.h"
#include "../core/datamanager.h"
#include "../widgets/videowidget.h"
#include "../theme.h"
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSettings>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>

VideoPage::VideoPage(QWidget *parent) : QWidget(parent)
{
    QSettings st;

    // ---- 控制行1: 来源切换 + 采集点 + 抓拍 ----
    m_btnSim = new QPushButton("模拟画面");
    m_btnSim->setObjectName("chartBtn");
    m_btnSim->setCheckable(true);
    m_btnFfmpeg = new QPushButton("FFmpeg");
    m_btnFfmpeg->setObjectName("chartBtn");
    m_btnFfmpeg->setCheckable(true);

    QLabel *l1 = new QLabel("采集点");
    m_devCombo = new QComboBox;
    const QList<int> ids = DataManager::instance().deviceIds();
    for (int id : ids)
        m_devCombo->addItem(DataManager::instance().deviceName(id), id);

    m_btnSnap = new QPushButton("抓 拍");
    m_btnSnap->setObjectName("queryBtn");

    QHBoxLayout *c1 = new QHBoxLayout;
    c1->setContentsMargins(0, 0, 0, 0);
    c1->setSpacing(4);
    c1->addWidget(m_btnSim);
    c1->addWidget(m_btnFfmpeg);
    c1->addSpacing(4);
    c1->addWidget(l1);
    c1->addWidget(m_devCombo);
    c1->addStretch();
    c1->addWidget(m_btnSnap);

    // ---- 控制行2: ffmpeg 命令 ----
    m_cmdEdit = new QLineEdit;
    m_cmdEdit->setPlaceholderText("ffmpeg 命令(输出 MJPEG 到 stdout), 例: ffmpeg -f mjpeg -i http://192.168.1.13:8090/stream.mjpeg -f image2pipe -vcodec mjpeg -q:v 4 -");
    QFont cf = m_cmdEdit->font();
    cf.setPixelSize(9);
    m_cmdEdit->setFont(cf);
    m_cmdEdit->setText(st.value("video/cmd",
        "ffmpeg -f mjpeg -i http://192.168.1.13:8090/stream.mjpeg -f image2pipe -vcodec mjpeg -q:v 4 -").toString());
    m_btnApply = new QPushButton("应用");
    m_btnApply->setObjectName("chartBtn");

    QHBoxLayout *c2 = new QHBoxLayout;
    c2->setContentsMargins(0, 0, 0, 0);
    c2->setSpacing(4);
    c2->addWidget(m_cmdEdit, 1);
    c2->addWidget(m_btnApply);

    // ---- 视频区 ----
    m_video = new VideoWidget;

    // ---- 状态行 ----
    m_status = new QLabel("");
    m_status->setObjectName("panelTitle");

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    lay->addLayout(c1);
    lay->addLayout(c2);
    lay->addWidget(m_video, 1);
    lay->addWidget(m_status);

    m_src = st.value("video/source", 0).toInt();
    if (m_src == 1) { m_btnFfmpeg->setChecked(true); }
    else            { m_btnSim->setChecked(true); m_src = 0; }

    connect(m_btnSim, &QPushButton::clicked, this, &VideoPage::onSourceSim);
    connect(m_btnFfmpeg, &QPushButton::clicked, this, &VideoPage::onSourceFfmpeg);
    connect(m_btnApply, &QPushButton::clicked, this, &VideoPage::onApplyCmd);
    connect(m_btnSnap, &QPushButton::clicked, this, &VideoPage::onSnap);
    connect(m_video, &VideoWidget::statusChanged, this, &VideoPage::onStatus);
    connect(m_video, &VideoWidget::sourceMessage, this, &VideoPage::onStatus);

    startCurrentSource();
}

void VideoPage::hideEvent(QHideEvent *)
{
    // 离开页面暂停拉流, 回来再恢复
    m_video->stopSource();
}

void VideoPage::showEvent(QShowEvent *)
{
    startCurrentSource();
}

int VideoPage::currentDeviceId() const
{
    return m_devCombo->currentData().toInt();
}

void VideoPage::onSourceSim()
{
    m_src = 0;
    m_btnSim->setChecked(true);
    m_btnFfmpeg->setChecked(false);
    QSettings st;
    st.setValue("video/source", 0);
    startCurrentSource();
}

void VideoPage::onSourceFfmpeg()
{
    m_src = 1;
    m_btnFfmpeg->setChecked(true);
    m_btnSim->setChecked(false);
    QSettings st;
    st.setValue("video/source", 1);
    startCurrentSource();
}

void VideoPage::onApplyCmd()
{
    QSettings st;
    st.setValue("video/cmd", m_cmdEdit->text().trimmed());
    if (m_src == 1)
        startCurrentSource();
    else
        onStatus("命令已保存, 切到 FFmpeg 来源后生效");
}

void VideoPage::startCurrentSource()
{
    m_video->setSource(m_src == 1 ? VideoWidget::FfmpegSource : VideoWidget::SimSource,
                       m_cmdEdit->text().trimmed());
    onStatus(m_video->statusText());
}

void VideoPage::onSnap()
{
    const QImage f = m_video->currentFrame();
    if (f.isNull()) {
        onStatus("抓拍失败: 当前无画面");
        return;
    }
    const int devid = currentDeviceId();
    const QString dir = QCoreApplication::applicationDirPath() + "/snaps";
    QDir().mkpath(dir);
    const QString path = QString("%1/snap_%2_%3.jpg")
                         .arg(dir).arg(devid)
                         .arg(QDateTime::currentMSecsSinceEpoch());
    f.save(path, "JPEG", 90);
    DataManager::instance().addSnapshot(devid, path, "手动");
    onStatus(QString("手动抓拍已保存: %1 (可在告警记录页查看)").arg(path));
}

void VideoPage::onStatus(const QString &text)
{
    m_status->setText(text);
}
