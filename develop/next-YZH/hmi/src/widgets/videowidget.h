#pragma once
#include <QWidget>
#include <QProcess>
#include <QTimer>
#include <QImage>

/*
 * 视频监控控件 V3: 双数据源
 *  - SimSource:    内置模拟产线画面(开发机无摄像头/ffmpeg 时演示用)
 *  - FfmpegSource: QProcess 启动 ffmpeg, 把任意输入(RTSP/HTTP-MJPEG/本地摄像头)
 *                  转成 MJPEG 写到 stdout, 本控件按 JPEG 帧界标记解析渲染。
 *                  不依赖 QtMultimedia, 嵌入式板与 Windows 通用。
 * 常用命令(可界面输入, 见项目文档-V3 §7):
 *   ffmpeg -f mjpeg -i http://192.168.1.13:8090/stream.mjpeg -f image2pipe -vcodec mjpeg -q:v 4 -
 *   ffmpeg -f dshow -vcodec mjpeg -i video="USB Camera"     -f image2pipe -vcodec copy  -
 */
class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    enum Source { SimSource = 0, FfmpegSource = 1 };

    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

    void setSource(Source src, const QString &ffmpegCmd = QString());
    void stopSource();
    QImage currentFrame() const;
    QString statusText() const;
    bool isSimSource() const { return m_src == SimSource; }

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

private:
    void extractFrames();
    void countFrame();
    QImage makeSimFrame();

    Source m_src = SimSource;
    QImage m_frame;
    QByteArray m_buf;
    QProcess *m_proc = nullptr;
    QTimer *m_simTimer = nullptr;
    int m_tick = 0;
    int m_frames = 0;
    qint64 m_windowStart = 0;
    int m_fps = 0;
    QSize m_res;
    QString m_errorMsg;
};
