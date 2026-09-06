#include "videowidget.h"
#include "../theme.h"
#include <QPainter>
#include <QDateTime>
#include <QDir>
#include <QtGlobal>
#include <QImageReader>   // supportedImageFormats: 探测板端 Qt 是否部署了 jpeg 图像插件

#ifdef Q_OS_LINUX
// Linux(板上)才有的头: 用 V4L2 接口探测/采集摄像头, Windows 编译时整段不参与
#include <fcntl.h>          // open/close
#include <unistd.h>         // read/write 等 Unix 系统调用
#include <sys/ioctl.h>      // ioctl: 向设备驱动发控制命令
#include <sys/mman.h>       // mmap/munmap: 内核缓冲映射到用户态
#include <linux/videodev2.h> // V4L2(Video4Linux2) 摄像头框架定义
#include <cstring>          // memset/strerror
#include <cerrno>           // errno: v4lStart 失败环节给出系统错误原因
#endif

VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(160, 120);
    m_simTimer = new QTimer(this);
    m_simTimer->setInterval(100);          // 模拟画面 10fps
    connect(m_simTimer, &QTimer::timeout, this, &VideoWidget::onSimTick);

    // 摄像头看门狗: 单次触发。某条打开路径(V4L2 或某套 ffmpeg 命令)一直
    // 出不了帧时, 超时后降级到下一候选
    m_camWatchdog = new QTimer(this);
    m_camWatchdog->setSingleShot(true);
    m_camWatchdog->setInterval(6000);      // 6 秒内没等到第一帧就判定失败
    connect(m_camWatchdog, &QTimer::timeout, this, &VideoWidget::onCamWatchdog);

#ifdef Q_OS_LINUX
    /* 【修改点11】原生 V4L2 采集帧轮询定时器(30fps 节拍):
     * DQBUF 用 O_NONBLOCK 非阻塞模式, 定时器在主线程轮询取帧,
     * 无帧立刻返回, 不会阻塞 UI; 有帧则解码刷新, 免去了 ffmpeg 进程依赖 */
    m_v4lTimer = new QTimer(this);
    m_v4lTimer->setInterval(33);
    connect(m_v4lTimer, &QTimer::timeout, this, &VideoWidget::onV4lTick);
#endif
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

/* ================= 本地摄像头: 原生 V4L2 优先, ffmpeg 兜底 ================= */
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

    /* 【修复点15】按 Qt JPEG 解码能力选择命令组:
     * 板上实测: ffmpeg 采集/编码正常, 但 Qt 若未部署 jpeg 图像插件
     * (plugins/imageformats/libqjpeg.so), QImage 解 JPEG 会静默失败,
     * 所有 MJPEG 路径都出不了画面 —— 这是"命令行能出图、界面全黑"的根因。
     *  - 有插件: 走 MJPEG(copy 直拷零编码 / mjpeg软编码), CPU 最省;
     *  - 无插件: 走 rawvideo bgr24 原始帧(ffmpeg 内部解码, 输出 BGR 字节流,
     *    本程序按 w*h*3 固定长度切帧, 直接组装 RGB32, 完全不依赖 JPEG 插件)。
     * 命令统一加 -video_size 640x480: 板子 528MHz, 默认 1280x720 软编码只有
     * 0.168x 速度(实测), 640x480 才带得动。 */
    const bool hasJpeg = QImageReader::supportedImageFormats().contains("jpeg");
    m_ffmpegRaw = !hasJpeg;
    m_rawW = 640;
    m_rawH = 480;
    m_camCandidates.clear();
    if (hasJpeg) {
        // ① MJPEG 直出(零拷贝转封装, CPU 最低)  ② 通用软编码兼容
        m_camCandidates << QString("ffmpeg -f v4l2 -input_format mjpeg -video_size 640x480 -framerate 10 -i %1 "
                                   "-f image2pipe -vcodec copy -").arg(dev)
                        << QString("ffmpeg -f v4l2 -video_size 640x480 -i %1 "
                                   "-f image2pipe -vcodec mjpeg -q:v 5 -").arg(dev);
    } else {
        // 无 JPEG 插件: ffmpeg 负责解码/缩放, 输出 bgr24 原始帧
        m_camCandidates << QString("ffmpeg -f v4l2 -input_format mjpeg -video_size 640x480 -i %1 "
                                   "-f rawvideo -pix_fmt bgr24 -").arg(dev)
                        << QString("ffmpeg -f v4l2 -video_size 640x480 -i %1 "
                                   "-f rawvideo -pix_fmt bgr24 -").arg(dev);
        emit sourceMessage("未检测到Qt JPEG插件, 已改用原始帧采集(bgr24), 无需图像插件即可显示");
    }
    m_camTry = 0;
    m_camWatchdog->start();                // 给首选路径 6 秒时间出第一帧

#ifdef Q_OS_LINUX
    /* 【修改点11续】首选原生 V4L2 mmap 采集:
     * 不依赖板上是否安装 ffmpeg(之前画面打不开的最常见原因就是板上没有
     * ffmpeg 或其版本不支持某参数), 直接 ioctl 打开设备拿帧。
     * 打开成功则等看门狗/帧到来; 失败则落到下方 ffmpeg 兜底。 */
    if (v4lStart(dev)) {
        emit statusChanged(statusText());
        return;
    }
    v4lStop();
    /* 【修改点14续】原生失败原因立即上屏: 用户能直接看到卡在哪个环节
     * (open/能力/格式协商/缓冲/STREAMON), 同时继续走 ffmpeg 兜底 */
    if (!m_v4lLastError.isEmpty())
        m_errorMsg = QString("V4L2: %1 (尝试ffmpeg兜底...)").arg(m_v4lLastError);
    emit sourceMessage(m_v4lLastError);
    emit statusChanged(statusText());
#endif

    tryNextCamCandidate();                 // 降级: ffmpeg 候选命令
    emit statusChanged(statusText());
}

void VideoWidget::tryNextCamCandidate()
{
    if (m_camTry >= m_camCandidates.size()) {
        // 全部路径都失败: 汇总各环节原因, 方便现场定位
        m_errorMsg = QString("摄像头 %1 启动失败").arg(m_camDev);
#ifdef Q_OS_LINUX
        /* 【修改点14续】带上 V4L2 失败的具体环节 */
        if (!m_v4lLastError.isEmpty())
            m_errorMsg += QString(" | V4L2: %1").arg(m_v4lLastError);
#endif
        if (!m_procErr.isEmpty())
            m_errorMsg += QString(" | ffmpeg: %1").arg(QString::fromLocal8Bit(m_procErr.right(120)).simplified());
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
    // 【修改点12】缓存 ffmpeg stderr 末尾内容: ffmpeg 的报错/进度都写 stderr,
    // 失败时把它透出到状态栏, 现场能直接看到"为什么打不开"
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        if (m_proc)
            m_procErr += m_proc->readAllStandardError();
        if (m_procErr.size() > 2048)
            m_procErr = m_procErr.right(1024);
    });
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
    m_procErr.clear();
    m_ffmpegRaw = false;                  // 【修复点15续】复位 raw 模式, 防止切源后误按定长切帧
    m_simTimer->stop();
#ifdef Q_OS_LINUX
    v4lStop();                             // 【修改点11续】同步关闭原生 V4L2 采集
#endif
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

// ---------------- FFmpeg ? ----------------

void VideoWidget::onReadyRead()
{
    if (!m_proc)
        return;
    m_buf += m_proc->readAllStandardOutput();
    if (m_buf.size() > 8 * 1024 * 1024)   // 防御: 长时间无有效帧
        m_buf.clear();
    /* 【修复点15续】raw 模式按固定帧长切帧, JPEG 模式按 marker 结构切帧 */
    if (m_ffmpegRaw)
        extractRawFrames();
    else
        extractFrames();
}

/* ================= rawvideo(bgr24) 帧解析 =================
 * ffmpeg 输出连续 BGR 字节流, 每帧固定 w*h*3 字节、无帧头帧尾,
 * 按协商分辨率切片即可。只保留最新帧, 积压直接丢弃(画面宁新不卡)。
 * 不依赖 Qt JPEG 插件, 是板端缺插件时的保底显示链路。 */
void VideoWidget::extractRawFrames()
{
    const int frameSize = m_rawW * m_rawH * 3;
    if (frameSize <= 0)
        return;
    while (m_buf.size() >= frameSize) {
        // 积压超过 3 帧: 丢到只剩最新一整帧, 避免慢 CPU 上画面越积越延迟
        const int framesAvail = m_buf.size() / frameSize;
        if (framesAvail > 2)
            m_buf.remove(0, (framesAvail - 1) * frameSize);

        const uchar *src = reinterpret_cast<const uchar *>(m_buf.constData());
        QImage img(m_rawW, m_rawH, QImage::Format_RGB32);
        for (int y = 0; y < m_rawH; ++y) {
            const uchar *s = src + y * m_rawW * 3;          // bgr24: B,G,R 交错
            QRgb *d = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < m_rawW; ++x)
                d[x] = qRgb(s[x * 3 + 2], s[x * 3 + 1], s[x * 3]);
        }
        if (!m_gotFrame) {
            m_gotFrame = true;
            m_camWatchdog->stop();
            m_errorMsg.clear();
        }
        m_frame = img;
        m_res = img.size();
        countFrame();
        m_buf.remove(0, frameSize);
    }
    if (m_gotFrame)
        update();
}

/* ================= JPEG 帧解析 =================
 * 【修改点13】结构化扫描替代朴素字节搜索:
 * 旧实现从 SOI(FF D8) 起搜第一个 FF D9 当帧尾 —— 但 FF D9 可能出现在
 * EXIF 等 APP 段的载荷字节里(伪 EOI), 会把一帧拦腰截断, QImage 解码
 * 失败, 表现为"ffmpeg 明明在出流但永远没有画面"。
 * 新实现按 JPEG marker 结构逐段走: 普通段跳段长, SOS 段后进入熵编码
 * 数据区(FF 00 是填充, FF Dn 是重启标记), 直到遇到真正的 EOI(FF D9)。 */

/* 从 m_buf(必须以 SOI 开头)扫描完整帧的长度; 数据未到齐返回 -1 */
int VideoWidget::scanJpegEoi() const
{
    const int n = m_buf.size();
    int i = 2;                             // 跳过 SOI(FF D8)
    while (i + 1 < n) {
        const uchar b0 = uchar(m_buf.at(i));
        if (b0 != 0xFF) { ++i; continue; }             // 容错: 段内杂字节
        const uchar m = uchar(m_buf.at(i + 1));
        if (m == 0xFF) { ++i; continue; }              // 连续填充 FF
        if (m == 0xD9) return i + 2;                   // EOI: 帧完整结束
        if (m == 0xD8) return i;                       // 异常流: 又一帧的SOI, 上帧截断兜底
        if (m >= 0xD0 && m <= 0xD7) { i += 2; continue; } // RSTn 重启标记: 无长度字段
        if (i + 3 >= n) return -1;                     // 2字节段长未到齐
        const int segLen = (uchar(m_buf.at(i + 2)) << 8) | uchar(m_buf.at(i + 3));
        if (m == 0xDA) {                               // SOS: 之后进入熵编码数据
            int j = i + 2 + segLen;                    // 熵编码数据起点
            while (j + 1 < n) {
                if (uchar(m_buf.at(j)) != 0xFF) { ++j; continue; }
                const uchar e = uchar(m_buf.at(j + 1));
                if (e == 0x00) { j += 2; continue; }             // FF 00 = 数据FF
                if (e >= 0xD0 && e <= 0xD7) { j += 2; continue; } // 熵内重启标记
                if (e == 0xD9) return j + 2;                     // 熵编码后的真 EOI
                return j;                                        // 其他marker: 按帧界处理
            }
            return -1;                                 // 熵编码还没结束: 等数据
        }
        i += 2 + segLen;                       // APPn/COM/DQT/SOF 等普通段整段跳过
    }
    return -1;
}

void VideoWidget::extractFrames()
{
    forever {
        const int soi = m_buf.indexOf("\xFF\xD8");
        if (soi < 0) {
            // 无帧头: 只留最后 1 字节防跨包拆分
            if (m_buf.size() > 1)
                m_buf.remove(0, m_buf.size() - 1);
            return;
        }
        if (soi > 0)
            m_buf.remove(0, soi);              // 丢弃帧头之前的垃圾字节

        const int frameLen = scanJpegEoi();    // 结构化扫描帧长
        if (frameLen < 0)
            return;                            // 半帧: 等下一次数据到齐

        const QByteArray jpg = m_buf.left(frameLen);
        QImage img;
        if (img.loadFromData(jpg, "JPEG")) {
            // 第一帧到达: 当前打开路径有效, 关掉看门狗不再降级重试
            if (!m_gotFrame) {
                m_gotFrame = true;
                m_camWatchdog->stop();
            }
            m_frame = img;
            m_res = img.size();
            countFrame();
        }
        m_buf.remove(0, frameLen);             // 已消费整帧
        if (m_buf.size() < 2)
            return;
    }
}

/* ================= 原生 V4L2 mmap 采集(仅 Linux) ================= */
#ifdef Q_OS_LINUX

/* 打开设备并启动采集, 成功返回 true。失败时 m_v4lLastError 记录具体环节。
 * 流程: open → QUERYCAP 校验 → ENUM_FMT 枚举驱动支持的格式 → S_FMT 协商
 *      → (流式)REQBUFS+mmap+QBUF+STREAMON / (老驱动)read() 裸读兜底
 *
 * 【修改点14-修复"识别到但无画面"】
 * 旧版只盲试 MJPEG/YUYV 两种 S_FMT —— 若驱动实际只支持 UYVY(野火 OV5640/
 * OV2640 的 mxc_v4l2 老驱动常见)或字节序不同的其他 YUV422, 协商失败 →
 * 降级 ffmpeg 也起不来 → 永远无画面。
 * 新版先用 VIDIOC_ENUM_FMT 枚举驱动真正支持的格式, 再按
 * MJPEG > YUYV > UYVY 的优先级从中挑选(优先解码开销最小的), 并支持
 * 无流式能力的老驱动用 read() 裸读; 每个失败环节写入具体原因显示在画面上。 */
bool VideoWidget::v4lStart(const QString &dev)
{
    m_v4lLastError.clear();
    m_v4lIdle = 0;

    m_v4lFd = ::open(dev.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
    if (m_v4lFd < 0) {
        m_v4lLastError = QString("open失败:%1").arg(strerror(errno));
        return false;
    }

    // 校验设备能力: 必须是采集设备, 且支持流式IO或read()两者之一
    struct v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));
    if (::ioctl(m_v4lFd, VIDIOC_QUERYCAP, &cap) != 0) {
        m_v4lLastError = "QUERYCAP失败(非V4L2设备)";
        ::close(m_v4lFd); m_v4lFd = -1;
        return false;
    }
    const bool canStream = (cap.capabilities & V4L2_CAP_STREAMING) != 0;
    const bool canRead   = (cap.capabilities & V4L2_CAP_READWRITE) != 0;
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || (!canStream && !canRead)) {
        m_v4lLastError = "设备无采集能力(或仅无流式/读能力)";
        ::close(m_v4lFd); m_v4lFd = -1;
        return false;
    }

    // ① 枚举驱动支持的像素格式(不再盲试, 对 UVC / mxc_v4l2 / 老驱动都稳)
    QList<quint32> drvFmts;
    QStringList drvNames;
    struct v4l2_fmtdesc fd;
    std::memset(&fd, 0, sizeof(fd));
    fd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (drvFmts.size() < 32 && ::ioctl(m_v4lFd, VIDIOC_ENUM_FMT, &fd) == 0) {
        drvFmts << fd.pixelformat;
        drvNames << QString::fromLatin1(reinterpret_cast<const char *>(fd.description)).simplified();
        ++fd.index;
    }

    // ② 从驱动支持的格式里按"解码开销从小到大"挑选。
    // 【修复点15续】MJPEG 依赖 Qt jpeg 插件解码 —— 板端若没部署插件,
    // QImage 解 JPEG 静默失败(帧到了却没画面)。故无插件时把 MJPEG
    // 从候选中剔除, 直接协商 YUYV/UYVY 原始格式由本类自己转 RGB。
    const bool hasJpeg = QImageReader::supportedImageFormats().contains("jpeg");
    struct FmtEntry { quint32 fourcc; int id; bool uyvy; bool needJpegPlugin; };
    const FmtEntry wantList[] = {
        { V4L2_PIX_FMT_MJPEG, V4L_FMT_MJPEG, false, true  },   // CPU 最省但要 jpeg 插件
        { V4L2_PIX_FMT_YUYV,  V4L_FMT_YUYV,  false, false },   // 自转换, 无依赖
        { V4L2_PIX_FMT_UYVY,  V4L_FMT_UYVY,  true,  false },   // mxc/OV 摄像头常见
    };
    bool fmtOk = false;
    for (const FmtEntry &w : wantList) {
        if (w.needJpegPlugin && !hasJpeg)
            continue;                          // 无 jpeg 插件: 跳过 MJPEG, 否则永远黑屏
        if (!drvFmts.contains(w.fourcc))
            continue;                          // 驱动不支持, 跳过
        struct v4l2_format f;
        std::memset(&f, 0, sizeof(f));
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        f.fmt.pix.width       = 640;           // 期望分辨率, 驱动可能就近调整
        f.fmt.pix.height      = 480;
        f.fmt.pix.pixelformat = w.fourcc;
        f.fmt.pix.field       = V4L2_FIELD_ANY;
        if (::ioctl(m_v4lFd, VIDIOC_S_FMT, &f) != 0)
            continue;                          // 该格式协商被拒
        if (f.fmt.pix.pixelformat != w.fourcc)
            continue;                          // 驱动表面接受实际换成了别的格式
        m_v4lFmt = w.id;
        m_v4lW = int(f.fmt.pix.width);
        m_v4lH = int(f.fmt.pix.height);
        fmtOk = true;
        break;
    }
    if (!fmtOk) {
        m_v4lLastError = QString("格式协商失败(驱动支持:%1)")
                         .arg(drvNames.isEmpty() ? "无" : drvNames.join("/"));
        ::close(m_v4lFd); m_v4lFd = -1;
        return false;
    }

    m_v4lReadMode = false;
    if (canStream) {
        // ---- 标准 mmap 流式采集 ----
        struct v4l2_requestbuffers req;
        std::memset(&req, 0, sizeof(req));
        req.count  = 4;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (::ioctl(m_v4lFd, VIDIOC_REQBUFS, &req) != 0 || req.count < 2) {
            m_v4lLastError = "REQBUFS失败(内存不足?)";
            ::close(m_v4lFd); m_v4lFd = -1;
            return false;
        }
        m_v4lBufs.resize(int(req.count));
        for (uint i = 0; i < req.count; ++i) {
            struct v4l2_buffer b;
            std::memset(&b, 0, sizeof(b));
            b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            b.index  = i;
            if (::ioctl(m_v4lFd, VIDIOC_QUERYBUF, &b) != 0) {
                m_v4lLastError = "QUERYBUF失败";
                v4lStop();
                return false;
            }
            m_v4lBufs[int(i)].length = size_t(b.length);
            m_v4lBufs[int(i)].start  =
                ::mmap(nullptr, b.length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, m_v4lFd, b.m.offset);
            if (m_v4lBufs[int(i)].start == MAP_FAILED) {
                m_v4lBufs[int(i)].start = nullptr;
                m_v4lLastError = "mmap失败";
                v4lStop();
                return false;
            }
        }
        for (uint i = 0; i < req.count; ++i) {
            struct v4l2_buffer b;
            std::memset(&b, 0, sizeof(b));
            b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            b.index  = i;
            if (::ioctl(m_v4lFd, VIDIOC_QBUF, &b) != 0) {
                m_v4lLastError = "QBUF失败";
                v4lStop();
                return false;
            }
        }
        enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(m_v4lFd, VIDIOC_STREAMON, &t) != 0) {
            m_v4lLastError = QString("STREAMON失败:%1").arg(strerror(errno));
            v4lStop();
            return false;
        }
    } else {
        // ---- read() 裸读兜底: 极老驱动只支持 V4L2_CAP_READWRITE ----
        struct v4l2_format g;
        std::memset(&g, 0, sizeof(g));
        g.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ::ioctl(m_v4lFd, VIDIOC_G_FMT, &g);
        quint32 frameSize = g.fmt.pix.sizeimage;
        if (frameSize == 0)
            frameSize = quint32(m_v4lW * m_v4lH * 2);   // YUV422 兜底估算
        m_v4lBuf.resize(int(frameSize));
        m_v4lReadMode = true;
    }

    m_v4lTimer->start(33);                     // 30fps 节拍轮询取帧
    return true;
}

/* 停流 + 解映射 + 释放缓冲 + 关闭设备(可安全重复调用) */
void VideoWidget::v4lStop()
{
    if (m_v4lTimer)
        m_v4lTimer->stop();
    if (m_v4lFd >= 0) {
        if (!m_v4lReadMode) {
            enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ::ioctl(m_v4lFd, VIDIOC_STREAMOFF, &t);
            for (int i = 0; i < m_v4lBufs.size(); ++i)
                if (m_v4lBufs[i].start)
                    ::munmap(m_v4lBufs[i].start, m_v4lBufs[i].length);
            struct v4l2_requestbuffers req;
            std::memset(&req, 0, sizeof(req));
            req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            req.memory = V4L2_MEMORY_MMAP;
            req.count  = 0;                    // 0 = 释放全部内核缓冲
            ::ioctl(m_v4lFd, VIDIOC_REQBUFS, &req);
        }
        ::close(m_v4lFd);
        m_v4lFd = -1;
    }
    m_v4lReadMode = false;
    m_v4lBuf.clear();
    m_v4lBufs.clear();
}

/* 定时器轮询: 非阻塞取一帧 → 解码 → 刷新, 无帧直接返回 */
void VideoWidget::onV4lTick()
{
    if (m_v4lFd < 0) {
        m_v4lTimer->stop();
        return;
    }

    QImage img;
    const uchar *data = nullptr;
    int dataSize = 0;
    if (m_v4lReadMode) {
        // read() 裸读模式: 无数据 EAGAIN, 下个 tick 再试
        ssize_t n = ::read(m_v4lFd, m_v4lBuf.data(), m_v4lBuf.size());
        if (n > 0) {
            data = reinterpret_cast<const uchar *>(m_v4lBuf.constData());
            dataSize = int(n);
        }
    } else {
        struct v4l2_buffer b;
        std::memset(&b, 0, sizeof(b));
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (::ioctl(m_v4lFd, VIDIOC_DQBUF, &b) != 0) {
            ++m_v4lIdle;                       // 暂无帧(EAGAIN)或错误
            if (m_v4lIdle == 150) {            // ~5 秒连续无帧: 给出诊断
                m_errorMsg = "流已开启但5秒无帧(设备被其他进程占用/传感器未出图)";
                emit sourceMessage(m_errorMsg);
            }
            return;
        }
        m_v4lIdle = 0;
        if (b.index < uint(m_v4lBufs.size()) && b.bytesused > 0) {
            data = reinterpret_cast<const uchar *>(m_v4lBufs[int(b.index)].start);
            dataSize = int(b.bytesused);
        }
        ::ioctl(m_v4lFd, VIDIOC_QBUF, &b);     // 缓冲立即回队, 驱动继续填新帧
    }

    // 按协商格式解码: MJPEG=帧即JPEG直接解码; YUYV/UYVY=手工转RGB
    if (data != nullptr && dataSize > 0) {
        if (m_v4lFmt == V4L_FMT_MJPEG)
            img.loadFromData(data, dataSize, "JPEG");
        else
            img = yuv422ToImage(data, m_v4lW, m_v4lH, m_v4lFmt == V4L_FMT_UYVY);
    }

    if (!img.isNull()) {
        if (!m_gotFrame) {                     // 第一帧: 当前路径成功, 停看门狗
            m_gotFrame = true;
            m_camWatchdog->stop();
            m_errorMsg.clear();
        }
        m_frame = img;
        m_res = img.size();
        countFrame();
        update();
    }
}

/* YUV422 打包格式 → RGB32。BT.601 整数近似, 每 2 像素共享一组 U/V。
 * uyvy=false: 字节序 Y0 U Y1 V (V4L2_PIX_FMT_YUYV)
 * uyvy=true : 字节序 U Y0 V Y1 (V4L2_PIX_FMT_UYVY, mxc_v4l2/OV5640 常见) */
QImage VideoWidget::yuv422ToImage(const uchar *src, int w, int h, bool uyvy)
{
    // 按 Y/U/V 在 4 字节组里的偏移适配两种字节序
    const int iY0 = uyvy ? 1 : 0;
    const int iU  = uyvy ? 0 : 1;
    const int iY1 = uyvy ? 3 : 2;
    const int iV  = uyvy ? 2 : 3;

    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        const uchar *line = src + y * w * 2;   // YUV422 每像素 2 字节
        QRgb *dst = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; x += 2) {
            const int Y0 = line[4 * (x / 2) + iY0];
            const int U  = line[4 * (x / 2) + iU] - 128;
            const int Y1 = line[4 * (x / 2) + iY1];
            const int V  = line[4 * (x / 2) + iV] - 128;
            // BT.601: R=Y+1.402V, G=Y-0.344U-0.714V, B=Y+1.772U (×256 取整)
            const int r0 = (298 * (Y0 - 16) + 409 * V + 128) >> 8;
            const int g0 = (298 * (Y0 - 16) - 100 * U - 208 * V + 128) >> 8;
            const int b0 = (298 * (Y0 - 16) + 516 * U + 128) >> 8;
            dst[x] = qRgb(qBound(0, r0, 255), qBound(0, g0, 255), qBound(0, b0, 255));
            if (x + 1 < w) {
                const int r1 = (298 * (Y1 - 16) + 409 * V + 128) >> 8;
                const int g1 = (298 * (Y1 - 16) - 100 * U - 208 * V + 128) >> 8;
                const int b1 = (298 * (Y1 - 16) + 516 * U + 128) >> 8;
                dst[x + 1] = qRgb(qBound(0, r1, 255), qBound(0, g1, 255), qBound(0, b1, 255));
            }
        }
    }
    return img;
}

#endif // Q_OS_LINUX

void VideoWidget::onCamWatchdog()
{
    // 看门狗到点: 当前打开路径 6 秒都没出一帧。
#ifdef Q_OS_LINUX
    /* 【修改点11续】V4L2 原生采集超时(极罕见, 通常设备被抢走):
     * 关闭原生采集, 降级到 ffmpeg 候选命令 */
    if (m_v4lFd >= 0) {
        v4lStop();
        m_errorMsg = "V4L2 采集无帧, 降级 ffmpeg 重试";
        emit sourceMessage(m_errorMsg);
        tryNextCamCandidate();                 // m_camCandidates 在 startLocalCamera 已备好
        return;
    }
#endif
    // ffmpeg 路径超时: 只负责 kill 进程; kill 会触发 finished 信号,
    // 降级重试统一在 onProcFinished() 里做, 避免两处同时推进候选命令
    if (m_proc)
        m_proc->kill();
}

void VideoWidget::onProcError(QProcess::ProcessError err)
{
    // FailedToStart: 进程根本没起来(最常见: 板上没装 ffmpeg), 不会再发 finished
    if (err == QProcess::FailedToStart) {
        if (m_errorMsg.isEmpty())
            m_errorMsg = QString("ffmpeg 启动失败(未安装或命令错误)");
        if (m_src == LocalCam && !m_gotFrame && m_camTry < m_camCandidates.size()) {
            if (m_proc) {
                m_proc->deleteLater();
                m_proc = nullptr;
            }
            tryNextCamCandidate();
            return;
        }
    } else if (err == QProcess::Crashed) {
        // 【修复点16】不覆盖前面的 V4L2 诊断(如"格式协商失败"), 只在为空时补提示;
        // 真正的失败原因以 onProcFinished 里 ffmpeg stderr 为准
        if (m_errorMsg.isEmpty())
            m_errorMsg = "ffmpeg 异常退出";
    } else {
        if (m_errorMsg.isEmpty())
            m_errorMsg = QString("ffmpeg 错误(%1)").arg(int(err));
    }
    emit sourceMessage(m_errorMsg);
    emit statusChanged(statusText());
    update();
}

void VideoWidget::onProcFinished(int code, QProcess::ExitStatus st)
{
    if (m_src != FfmpegSource && m_src != LocalCam)
        return;

    // 【修复点16】进程退出时 stderr 管道里往往还残留最后一段错误输出
    // (ffmpeg 秒退时 readyRead 信号可能还没来得及处理), 这里兜底捞尽,
    // 否则失败原因永远为空, 界面只剩一句没头没尾的"ffmpeg 异常退出"
    if (m_proc)
        m_procErr += m_proc->readAllStandardError();

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

    if (m_errorMsg.isEmpty())
        m_errorMsg = (st == QProcess::CrashExit) ? "视频流中断"
                                                 : QString("ffmpeg 退出(码%1)").arg(code);

    // 汇总 ffmpeg stderr 最有价值的末行(具体报错都在这里)
    if (!m_procErr.isEmpty()) {
        const QString tail = QString::fromLocal8Bit(m_procErr.right(400)).simplified();
        if (!tail.isEmpty())
            m_errorMsg += QString(" | %1").arg(tail.right(160));
    }
    // 【修复点16续】V4L2 原生采集若也失败过, 原因一并保留显示
#ifdef Q_OS_LINUX
    if (!m_v4lLastError.isEmpty())
        m_errorMsg += QString(" | V4L2: %1").arg(m_v4lLastError);
#endif
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
