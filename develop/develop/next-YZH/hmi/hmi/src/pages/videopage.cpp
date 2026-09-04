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
    m_btnCam = new QPushButton("摄像头");
    m_btnCam->setObjectName("chartBtn");
    m_btnCam->setCheckable(true);

    // 摄像头设备下拉框: 开机自动扫描 /dev/video* 填充
    m_camCombo = new QComboBox;
    m_camCombo->setMinimumContentsLength(12);
    rescanCameras();

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
    c1->addWidget(m_btnCam);
    c1->addWidget(m_camCombo);
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

    m_src = st.value("video/source", -1).toInt();
    /* 板上自动启动策略:
     * - Linux(开发板)上第一次运行(没有保存过来源选择)且识别到摄像头:
     *   自动切到"本地摄像头"来源, 开机即出画面, 无需任何人工操作;
     * - 用户手动切过来源后按保存的选择启动; Windows 开发机无摄像头时回落模拟画面。 */
    if (m_src == -1) {
        if (!VideoWidget::detectCameras().isEmpty())
            m_src = 2;                       // 自动识别到摄像头 → 默认打开
        else
            m_src = 0;
        st.setValue("video/source", m_src);
    }

    connect(m_btnSim, &QPushButton::clicked, this, &VideoPage::onSourceSim);
    connect(m_btnFfmpeg, &QPushButton::clicked, this, &VideoPage::onSourceFfmpeg);
    connect(m_btnCam, &QPushButton::clicked, this, &VideoPage::onSourceCam);
    connect(m_camCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) {
                // 换选设备: 若正处于摄像头模式, 立即切到新设备
                if (m_src == 2)
                    startCurrentSource();
            });
    connect(m_btnApply, &QPushButton::clicked, this, &VideoPage::onApplyCmd);
    connect(m_btnSnap, &QPushButton::clicked, this, &VideoPage::onSnap);
    connect(m_video, &VideoWidget::statusChanged, this, &VideoPage::onStatus);
    connect(m_video, &VideoWidget::sourceMessage, this, &VideoPage::onStatus);

    // 按钮选中态与 m_src 对齐
    m_btnSim->setChecked(m_src == 0);
    m_btnFfmpeg->setChecked(m_src == 1);
    m_btnCam->setChecked(m_src == 2);

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
    m_btnCam->setChecked(false);
    QSettings st;
    st.setValue("video/source", 0);
    startCurrentSource();
}

void VideoPage::onSourceFfmpeg()
{
    m_src = 1;
    m_btnFfmpeg->setChecked(true);
    m_btnSim->setChecked(false);
    m_btnCam->setChecked(false);
    QSettings st;
    st.setValue("video/source", 1);
    startCurrentSource();
}

void VideoPage::onSourceCam()
{
    // 每次点"摄像头"都重新扫描一次设备(支持运行中热插拔后补扫)
    rescanCameras();
    if (m_camCombo->count() == 0) {
        onStatus("未识别到摄像头设备(检查 USB 连接/驱动: ls /dev/video*)");
        m_btnCam->setChecked(false);
        return;
    }
    m_src = 2;
    m_btnCam->setChecked(true);
    m_btnSim->setChecked(false);
    m_btnFfmpeg->setChecked(false);
    QSettings st;
    st.setValue("video/source", 2);
    startCurrentSource();
}

void VideoPage::rescanCameras()
{
    // V4L2 能力探测: 只列出真正支持"视频采集"的 /dev/videoN 节点
    const QStringList cams = VideoWidget::detectCameras();
    m_camCombo->clear();
    if (cams.isEmpty()) {
        m_camCombo->addItem("未识别到");
        m_camCombo->setEnabled(false);
    } else {
        for (const QString &dev : cams)
            m_camCombo->addItem(dev);
        m_camCombo->setEnabled(true);
    }
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
    if (m_src == 2) {
        // 本地摄像头: 打开下拉框当前选中的 /dev/videoN, 自动组装 ffmpeg 命令
        if (m_camCombo->currentText().startsWith("/dev/"))
            m_video->startLocalCamera(m_camCombo->currentText());
        else
            m_video->stopSource();
    } else {
        m_video->setSource(m_src == 1 ? VideoWidget::FfmpegSource : VideoWidget::SimSource,
                           m_cmdEdit->text().trimmed());
    }
    onStatus(m_video->statusText());
}

void VideoPage::onSnap()
{
#ifdef HMI_DISABLE_SNAPSHOT
    // 板上精简模式(HMI_LITE): 抓拍功能禁用
    onStatus("精简模式下抓拍功能已禁用");
    Q_UNUSED(m_video);
#else
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
#endif
}

void VideoPage::onStatus(const QString &text)
{
    m_status->setText(text);
}
