#include "videowidget.h"
#include "../theme.h"
#include <QPainter>
#include <QDateTime>
#include <QDir>
#include <QtGlobal>

#ifdef Q_OS_LINUX
// Linux(板上)才有的头: 用 V4L2 接口探测摄像头能力, Windows 编译时整段不参与
#include <fcntl.h>          // open/close
#include <unistd.h>         // read/write 等 Unix 系统调用
#include <sys/ioctl.h>      // ioctl: 向设备驱动发控制命令
#include <linux/videodev2.h> // V4L2(Video4Linux2) 摄像头框架定义
#include <cstring>          // memset
#endif

VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(160, 120);
    m_simTimer = new QTimer(this);
    m_simTimer->setInterval(100);          // 模拟画面 10fps
    connect(m_simTimer, &QTimer::timeout, this, &VideoWidget::onSimTick);

    // 摄像头看门狗: 单次触发。某些命令 ffmpeg 起来了却一直出不了帧
    // (比如摄像头不支持该像素格式), 靠它超时后换下一套候选命令
    m_camWatchdog = new QTimer(this);
    m_camWatchdog->setSingleShot(true);
    m_camWatchdog->setInterval(6000);      // 6 秒内没等到第一帧就判定失败
    connect(m_camWatchdog, &QTimer::timeout, this, &VideoWidget::onCamWatchdog);
}

VideoWidget::~VideoWidget()
{
    stopSource();
}

/* ================= 摄像头自动识别 =================
 * 原理(面试常问): UVC 摄像头插上后, 内核驱动会在 /dev/ 下生成 videoN 节点。
 * 但较新内核里一个摄像头可能占多个节点(一个是采集节点, 一个是 metadata 元数据节点),
 * 只看文件名不够, 必须用 V4L2 的 VIDIOC_QUERYCAP ioctl 查询设备能力,
 * 只保留带 V4L2_CAP_VIDEO_CAPTURE(视频采集)能力的节点。
 */
QStringList VideoWidget::detectCameras()
{
    QStringList out;
#ifdef Q_OS_LINUX
    // QDir::System 才能列出 /dev 下的设备文件; QDir::Name 保证 video0 在 video1 前
    const QStringList nodes =
        QDir("/dev").entryList(QStringList("video*"), QDir::System, QDir::Name);
    for (const QString &name : nodes) {
        const QString path = "/dev/" + name;
        // 1. 打开设备节点(O_RDWR: V4L2 要求读写方式打开才能 ioctl)
        int fd = ::open(path.toLocal8Bit().constData(), O_RDWR);
        if (fd < 0)
            continue;                      // 打不开(被占用/权限不足)直接跳过
        // 2. 查询设备能力
        struct v4l2_capability cap;
        std::memset(&cap, 0, sizeof(cap));
        bool isCapture = false;
        if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            // device_caps 是内核 3.3+ 的精确能力字段; 老内核退回查 capabilities
            if (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)
                isCapture = true;
            else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
                isCapture = true;
        }
        // 二次验证(实战踩坑): 部分内核里 UVC 的 metadata 节点在 QUERYCAP 里
        // 也声称有采集能力, 但真正按"采集类型"要格式时会失败。
        // 所以再用 G_FMT 以 V4L2_BUF_TYPE_VIDEO_CAPTURE 类型探一次,
        // 要不到格式的节点一律排除 —— 这是判断"能不能出画面"的最终标准。
        if (isCapture) {
            struct v4l2_format fmt;
            std::memset(&fmt, 0, sizeof(fmt));
            fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (::ioctl(fd, VIDIOC_G_FMT, &fmt) != 0)
                isCapture = false;         // 拿不到采集格式 -> 不是真采集节点
        }
        ::close(fd);
        if (isCapture)
            out << path;                   // 是采集设备, 收进结果
    }
#endif
    return out;
}

void VideoWidget::setSource(Source src, const QString &ffmpegCmd)
{
    stopSource();
    m_src = src;
    m_localCam = (src == LocalCam);        // 记录模式, 供 onProcFinished 判断是否要降级重试
    m_errorMsg.clear();
    m_buf.clear();
    m_frames = 0;
    m_fps = 0;
    m_gotFrame = false;
    m_windowStart = QDateTime::currentMSecsSinceEpoch();

    if (m_src == SimSource) {
        m_simTimer->start();
        onSimTick();
    } else if (m_src == FfmpegSource) {
        startFfmpeg(ffmpegCmd);            // 网络流: 命令原样来自界面输入框
    }
    // LocalCam 走 startLocalCamera(), 不从这里进
    emit statusChanged(statusText());
}

/* ================= 本地摄像头: 组装候选命令并启动 ================= */
void VideoWidget::startLocalCamera(const QString &dev)
{
    if (dev.isEmpty()) {
        m_errorMsg = "未指定摄像头设备";
        emit sourceMessage(m_errorMsg);
        emit statusChanged(statusText());
        update();
        return;
    }

    stopSource();
    m_src = LocalCam;
    m_localCam = true;
    m_camDev = dev;
    m_errorMsg.clear();
    m_buf.clear();
    m_frames = 0;
    m_fps = 0;
    m_gotFrame = false;
    m_windowStart = QDateTime::currentMSecsSinceEpoch();

    /* 候选命令从优到劣排序:
     * ① MJPEG 直出(零拷贝转封装): 板子 CPU 只有 528MHz, 摄像头若支持 MJPEG
     *    则 ffmpeg 只做"转封装"不做编码, CPU 占用最低, 首选。
     * ② 通用采集+软编码: 不指定 input_format, 让 ffmpeg 选默认格式(多为 YUYV),
     *    再软编码成 MJPEG。兼容所有 UVC 摄像头, 但吃 CPU, 分辨率高时会卡。
     * 两条命令都会把结果以 MJPEG 流写到 stdout(- 结尾), 交给本控件的帧解析器。
     */
    m_camCandidates.clear();
    m_camCandidates << QString("ffmpeg -f v4l2 -input_format mjpeg -i %1 "
                               "-f image2pipe -vcodec copy -").arg(dev)
                    << QString("ffmpeg -f v4l2 -i %1 "
                               "-f image2pipe -vcodec mjpeg -q:v 4 -").arg(dev);
    m_camTry = 0;
    tryNextCamCandidate();
    emit statusChanged(statusText());
}

void VideoWidget::tryNextCamCandidate()
{
    if (m_camTry >= m_camCandidates.size()) {
        // 全部候选都失败: 摄像头存在但起不了流
        m_errorMsg = QString("摄像头 %1 启动失败(格式均不支持或被占用)").arg(m_camDev);
        emit sourceMessage(m_errorMsg);
        emit statusChanged(statusText());
        update();
        return;
    }
    startFfmpeg(m_camCandidates.at(m_camTry));
    ++m_camTry;
    m_camWatchdog->start();                // 给这套命令 6 秒时间出第一帧
}

/* 统一的 ffmpeg 进程启动: 网络流和本地摄像头共用 */
void VideoWidget::startFfmpeg(const QString &cmd)
{
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels); // stderr 不污染视频流
    connect(m_proc, &QProcess::readyReadStandardOutput,
            this, &VideoWidget::onReadyRead);
    connect(m_proc, static_cast<void(QProcess::*)(QProcess::ProcessError)>(&QProcess::errorOccurred),
            this, &VideoWidget::onProcError);
    connect(m_proc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &VideoWidget::onProcFinished);
    m_proc->start(cmd);                    // QProcess 会按空格分词后 exec
}

void VideoWidget::stopSource()
{
    m_camWatchdog->stop();                 // 别让看门狗在停止后还开火
    m_camCandidates.clear();
    m_camTry = 0;
    m_localCam = false;
    m_gotFrame = false;
    m_camDev.clear();
    m_simTimer->stop();
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(1000);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

QImage VideoWidget::currentFrame() const
{
    return m_frame;
}

QString VideoWidget::statusText() const
{
    QString src;
    if (m_src == SimSource)
        src = "模拟画面";
    else if (m_src == FfmpegSource)
        src = "FFmpeg";
    else
        src = QString("本地摄像头(%1)").arg(m_camDev.isEmpty() ? "未识别" : m_camDev);
    if (!m_errorMsg.isEmpty())
        return QString("来源: %1 | %2").arg(src, m_errorMsg);
    if (m_res.isEmpty())
        return QString("来源: %1 | 等待画面...").arg(src);
    return QString("来源: %1 | %2x%3 @ %4fps").arg(src)
           .arg(m_res.width()).arg(m_res.height()).arg(m_fps);
}

void VideoWidget::countFrame()
{
    ++m_frames;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_windowStart == 0)
        m_windowStart = now;
    if (now - m_windowStart >= 1000) {
        m_fps = m_frames;
        m_frames = 0;
        m_windowStart = now;
        emit statusChanged(statusText());
    }
}

// ---------------- 模拟画面源 ----------------

void VideoWidget::onSimTick()
{
    m_frame = makeSimFrame();
    m_res = m_frame.size();
    countFrame();
    update();
}

QImage VideoWidget::makeSimFrame()
{
    ++m_tick;
    const int w = 320, h = 240;
    QImage img(w, h, QImage::Format_RGB32);
    QPainter p(&img);
    p.fillRect(img.rect(), QColor(0x10, 0x16, 0x1d));

    // 背景墙
    p.setPen(QColor(0x1a, 0x24, 0x2e));
    for (int x = 0; x < w; x += 32)
        p.drawLine(x, 0, x, h * 55 / 100);

    // 传送带
    const int beltY = h * 58 / 100, beltH = 34;
    p.fillRect(0, beltY, w, beltH, QColor(0x2b, 0x3a, 0x48));
    p.setPen(QColor(0x3a, 0x4c, 0x5c));
    for (int x = -(m_tick * 3) % 24; x < w; x += 24)
        p.drawLine(x, beltY + beltH - 6, x + 10, beltY + beltH - 6);

    // 移动的料箱
    for (int i = 0; i < 4; ++i) {
        int bx = (i * 95 + m_tick * 3) % (w + 60) - 40;
        int by = beltY - 26;
        p.fillRect(bx, by, 34, 26, QColor(0xd8, 0xa0, 0x12));
        p.fillRect(bx, by, 34, 6, QColor(0xb5, 0x84, 0x0c));
        p.setPen(QColor(0x8a, 0x62, 0x08));
        p.drawRect(bx, by, 34, 26);
    }

    // 传感器支架示意
    p.fillRect(w - 14, beltY - 70, 6, 50, QColor(0x00, 0xc8, 0x96));

    // OSD 叠加
    QFont f = p.font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0x00, 0xa8, 0xff));
    p.drawText(8, 16, "CAM-1 · 产线监控 (模拟)");
    f.setBold(false);
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(QColor(0xd8, 0xe2, 0xea));
    p.drawText(8, h - 10, QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    p.setPen(QColor(0xe7, 0x4c, 0x3c));
    p.drawEllipse(w - 16, 8, 7, 7);
    p.setPen(QColor(0xe7, 0x4c, 0x3c));
    p.drawText(w - 64, 16, "REC");
    p.end();
    return img;
}

// ---------------- FFmpeg 源 ----------------

void VideoWidget::onReadyRead()
{
    if (!m_proc)
        return;
    m_buf += m_proc->readAllStandardOutput();
    if (m_buf.size() > 8 * 1024 * 1024)   // 防御: 长时间无有效帧
        m_buf.clear();
    extractFrames();
}

void VideoWidget::extractFrames()
{
    int consumed = 0;
    forever {
        const int soi = m_buf.indexOf("\xFF\xD8", consumed);
        if (soi < 0) {
            // 无帧头: 只留最后 1 字节防拆分
            if (m_buf.size() > 1)
                consumed = m_buf.size() - 1;
            break;
        }
        const int eoi = m_buf.indexOf("\xFF\xD9", soi + 2);
        if (eoi < 0) {
            consumed = soi;                 // 帧尾未到, 保留从帧头开始的数据
            break;
        }
        const QByteArray jpg = m_buf.mid(soi, eoi - soi + 2);
        QImage img;
        if (img.loadFromData(jpg, "JPEG")) {
            // 第一帧到达: 说明当前 ffmpeg 命令能出画面, 关掉看门狗不再降级重试
            if (!m_gotFrame) {
                m_gotFrame = true;
                m_camWatchdog->stop();
            }
            m_frame = img;
            m_res = img.size();
            countFrame();
        }
        consumed = eoi + 2;
    }
    if (consumed > 0)
        m_buf.remove(0, consumed);
    update();
}

void VideoWidget::onCamWatchdog()
{
    // 看门狗到点: 当前命令 6 秒都没出一帧。
    // 这里只负责 kill 进程; kill 会触发 finished 信号,
    // 降级重试统一在 onProcFinished() 里做, 避免两处同时推进候选命令。
    if (m_proc)
        m_proc->kill();
}

void VideoWidget::onProcError(QProcess::ProcessError err)
{
    // FailedToStart: 进程根本没起来, 不会再发 finished 信号,
    // 本地摄像头模式下在这里推进到下一套候选命令
    if (err == QProcess::FailedToStart) {
        m_errorMsg = "ffmpeg 启动失败(未安装或命令错误)";
        if (m_src == LocalCam && !m_gotFrame && m_camTry < m_camCandidates.size()) {
            if (m_proc) {
                m_proc->deleteLater();
                m_proc = nullptr;
            }
            tryNextCamCandidate();
            return;
        }
    } else if (err == QProcess::Crashed) {
        m_errorMsg = "ffmpeg 异常退出";
    } else {
        m_errorMsg = QString("ffmpeg 错误(%1)").arg(int(err));
    }
    emit sourceMessage(m_errorMsg);
    emit statusChanged(statusText());
    update();
}

void VideoWidget::onProcFinished(int, QProcess::ExitStatus st)
{
    if (m_src != FfmpegSource && m_src != LocalCam)
        return;

    // 本地摄像头模式: 若这套命令一条帧都没出就退了/被杀, 且还有候选命令,
    // 说明是格式不匹配(如摄像头不支持 MJPEG), 静默降级到下一套, 不算错误
    if (m_src == LocalCam && !m_gotFrame && m_camTry < m_camCandidates.size()) {
        if (m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        tryNextCamCandidate();
        return;
    }

    if (m_errorMsg.isEmpty() && st == QProcess::CrashExit)
        m_errorMsg = "视频流中断";
    if (m_errorMsg.isEmpty())
        m_errorMsg = "流已结束";
    emit sourceMessage(m_errorMsg);
    emit statusChanged(statusText());
    update();
}

// ---------------- 绘制 ----------------

void VideoWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(rect(), QColor(0x0a, 0x0e, 0x13));

    if (!m_frame.isNull()) {
        // 等比铺满(信箱式)
        QImage scaled = m_frame.scaled(size(), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        p.drawImage(QRect(x, y, scaled.width(), scaled.height()), scaled);
        p.setPen(QPen(Theme::PanelLine, 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    } else {
        p.setPen(Theme::Dim);
        p.drawText(rect(), Qt::AlignCenter,
                   m_errorMsg.isEmpty() ? "暂无画面 · 等待视频流..." : m_errorMsg);
    }
}
