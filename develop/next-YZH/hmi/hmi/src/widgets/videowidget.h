#pragma once
#include <QWidget>
#include <QProcess>
#include <QTimer>
#include <QImage>

#ifdef Q_OS_LINUX
#include <sys/types.h>   // size_t(与 videodev2 头依赖配套)
#endif

/*
 * 视频监控控件 V5: 三数据源
 *  - SimSource:    内置模拟产线画面(开发机无摄像头/ffmpeg 时演示用)
 *  - FfmpegSource: QProcess 启动 ffmpeg, 把任意输入(RTSP/HTTP-MJPEG等)
 *                  转成 MJPEG 写到 stdout, 本控件按 JPEG 帧界标记解析渲染。
 *                  不依赖 QtMultimedia, 嵌入式板与 Windows 通用。
 *  - LocalCam:     本地 USB 摄像头(板上 /dev/videoN), 两级打开策略:
 *      ①【首选】原生 V4L2 mmap 采集(Linux): 直接 ioctl 打开设备,
 *         优先要 MJPEG 格式(拿到就是已编码帧, QImage 直接解码),
 *         不支持再要 YUYV(手工转 RGB), 完全不依赖板上是否安装 ffmpeg;
 *      ②【兜底】ffmpeg v4l2 命令: 原生采集起不来时按候选命令降级重试
 *         (MJPEG 直出 → YUYV 软编码)。
 *    ffmpeg 的 stderr 会缓存末尾几行, 失败时显示具体原因, 方便现场排查。
 *
 * JPEG 帧解析(结构化扫描): 不再简单搜 FF D8/FF D9 字节对 —— 伪 EOI
 * (FF D9 出现在 EXIF 等 APP 段载荷里)会把一帧截成两半导致永远解不出图。
 * 现按 JPEG marker 结构逐段扫描, 穿过 SOS 熵编码数据找真正的 EOI。
 *
 * 常用命令(可界面输入, 见项目文档-V3 §7):
 *   ffmpeg -f mjpeg -i http://192.168.1.13:8090/stream.mjpeg -f image2pipe -vcodec mjpeg -q:v 4 -
 *   ffmpeg -f dshow -vcodec mjpeg -i video="USB Camera"     -f image2pipe -vcodec copy  -
 */
class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    enum Source { SimSource = 0, FfmpegSource = 1, LocalCam = 2 };

    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

    // 扫描 /dev/video*, 只返回真正支持"视频采集"能力的节点(Linux 有效, 其他平台返回空)
    static QStringList detectCameras();

    void setSource(Source src, const QString &ffmpegCmd = QString());
    void startLocalCamera(const QString &dev);   // 打开指定本地摄像头(原生V4L2优先, ffmpeg兜底)
    void stopSource();
    QImage currentFrame() const;
    QString statusText() const;
    bool isSimSource() const { return m_src == SimSource; }
    QString localCamDevice() const { return m_camDev; }

signals:
    void statusChanged(const QString &text);
    void sourceMessage(const QString &msg);   // 启动失败/错误等提示

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onSimTick();
    void onReadyRead();
    void onProcError(QProcess::ProcessError err);
    void onProcFinished(int code, QProcess::ExitStatus st);
    void onCamWatchdog();                 // 摄像头看门狗: 超时无帧则降级重试
#ifdef Q_OS_LINUX
    void onV4lTick();                     // 【修复】原生V4L2采集: 33ms轮询DQBUF取帧(实现与调用都在Q_OS_LINUX内, 声明必须同条件编译, 否则报 not a member)
#endif

private:
#ifdef Q_OS_LINUX
    /* ---- 原生 V4L2 mmap 采集(仅 Linux 参与) ---- */
    struct V4l2Buf { void *start = nullptr; size_t length = 0; };
    enum V4lFmt { V4L_FMT_MJPEG = 0, V4L_FMT_YUYV = 1, V4L_FMT_UYVY = 2 };

    bool v4lStart(const QString &dev);    // 打开设备+枚举格式+协商+映射缓冲+开始采集
    void v4lStop();                       // 停流+解映射+释放缓冲+关设备
    QImage yuv422ToImage(const uchar *src, int w, int h, bool uyvy);   // YUV422→RGB32(整型近似)

    QVector<V4l2Buf> m_v4lBufs;           // mmap 内核缓冲(与驱动共享)
    int m_v4lFd = -1;                     // 设备描述符, -1=未打开
    QTimer *m_v4lTimer = nullptr;         // 33ms 轮询 DQBUF 取帧(非阻塞, 不卡UI)
    int m_v4lFmt = V4L_FMT_MJPEG;         // 协商成功的格式(MJPEG/YUYV/UYVY)
    int m_v4lW = 0, m_v4lH = 0;           // 驱动实际生效的分辨率
    bool m_v4lReadMode = false;           // true=驱动不支持流式IO, 用 read() 裸读兜底
    QByteArray m_v4lBuf;                  // read 模式的整帧缓冲
    int m_v4lIdle = 0;                    // 连续无帧的轮询次数(超5秒给出诊断提示)
    QString m_v4lLastError;               // 最近一次 V4L2 启动失败的具体环节(全失败时显示)
#endif

    void extractFrames();                 // 从 m_buf 中解析完整 JPEG 帧
    void extractRawFrames();              // 【修复】ffmpeg rawvideo(bgr24) 模式: 按固定帧长 w*h*3 切帧
    int scanJpegEoi() const;              // 结构化扫描: 返回整帧长度(含EOI), 未到齐返回-1
    void countFrame();
    bool m_ffmpegRaw = false;             // ffmpeg 是否走 rawvideo 原始格式(板端缺Qt JPEG插件时用, 不依赖解码器)
    int m_rawW = 0, m_rawH = 0;           // raw 模式协商的分辨率(帧长 = w*h*3)
    QImage makeSimFrame();
    void startFfmpeg(const QString &cmd); // 内部统一起进程, 供网络流/本地摄像头复用
    void tryNextCamCandidate();           // 尝试下一套本地摄像头 ffmpeg 命令

    Source m_src = SimSource;
    QImage m_frame;
    QByteArray m_buf;
    QProcess *m_proc = nullptr;
    QTimer *m_simTimer = nullptr;
    QTimer *m_camWatchdog = nullptr;      // 单次触发: N 秒内无有效帧判定失败→降级
    QStringList m_camCandidates;          // 本地摄像头的候选 ffmpeg 命令(MJPEG直出→YUYV软编码)
    int m_camTry = 0;                     // 当前尝试到第几套候选命令
    bool m_localCam = false;              // 当前是否处于本地摄像头模式
    bool m_gotFrame = false;              // 本轮启动以来是否已解出有效帧
    QString m_camDev;                     // 当前使用的摄像头设备(如 /dev/video0)
    QByteArray m_procErr;                 // ffmpeg stderr 末尾内容(失败时显示原因)
    int m_tick = 0;
    int m_frames = 0;
    qint64 m_windowStart = 0;
    int m_fps = 0;
    QSize m_res;
    QString m_errorMsg;
};
