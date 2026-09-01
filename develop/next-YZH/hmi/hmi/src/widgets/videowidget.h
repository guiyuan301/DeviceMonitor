#pragma once
#include <QWidget>
#include <QProcess>
#include <QTimer>
#include <QImage>

/*
 * 视频监控控件 V4: 三数据源
 *  - SimSource:    内置模拟产线画面(开发机无摄像头/ffmpeg 时演示用)
 *  - FfmpegSource: QProcess 启动 ffmpeg, 把任意输入(RTSP/HTTP-MJPEG等)
 *                  转成 MJPEG 写到 stdout, 本控件按 JPEG 帧界标记解析渲染。
 *                  不依赖 QtMultimedia, 嵌入式板与 Windows 通用。
 *  - LocalCam:     本地 USB 摄像头(板上 /dev/videoN):
 *                  先用 V4L2 的 VIDIOC_QUERYCAP 自动识别"可采集"的视频节点,
 *                  再自动组装 ffmpeg v4l2 命令打开摄像头, 只预览不录像。
 *                  若摄像头不支持 MJPEG 直出, 自动降级为 YUYV 采集+软编码重试。
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
    void startLocalCamera(const QString &dev);   // 打开指定本地摄像头(自动组装 ffmpeg 命令)
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
    void onCamWatchdog();                 // 摄像头看门狗: 超时无帧则换下一套候选命令

private:
    void extractFrames();
    void countFrame();
    QImage makeSimFrame();
    void startFfmpeg(const QString &cmd); // 内部统一起进程, 供网络流/本地摄像头复用
    void tryNextCamCandidate();           // 尝试下一套本地摄像头 ffmpeg 命令

    Source m_src = SimSource;
    QImage m_frame;
    QByteArray m_buf;
    QProcess *m_proc = nullptr;
    QTimer *m_simTimer = nullptr;
    QTimer *m_camWatchdog = nullptr;      // 单次触发: N 秒内无有效帧判定该命令失败
    QStringList m_camCandidates;          // 本地摄像头的候选 ffmpeg 命令(MJPEG直出→YUYV软编码)
    int m_camTry = 0;                     // 当前尝试到第几套候选命令
    bool m_localCam = false;              // 当前是否处于本地摄像头模式
    bool m_gotFrame = false;              // 本轮启动以来是否已解出有效帧
    QString m_camDev;                     // 当前使用的摄像头设备(如 /dev/video0)
    int m_tick = 0;
    int m_frames = 0;
    qint64 m_windowStart = 0;
    int m_fps = 0;
    QSize m_res;
    QString m_errorMsg;
};
