#pragma once
#include <QWidget>

class QComboBox;
class QPushButton;
class QLabel;
class QLineEdit;
class VideoWidget;

/*
 * 视频监控页 V4: 实时画面(本地摄像头/FFmpeg拉流/模拟三源) + 手动抓拍。
 *  - 板上(Linux)启动时自动扫描 /dev/video* 并默认打开本地摄像头, 只预览不录像;
 *  - ffmpeg 网络流命令与来源选择用 QSettings 持久化。
 */
class VideoPage : public QWidget
{
    Q_OBJECT
public:
    explicit VideoPage(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void onSourceSim();
    void onSourceFfmpeg();
    void onSourceCam();       // 本地摄像头: 重新扫描设备并打开
    void onApplyCmd();
    void onSnap();
    void onStatus(const QString &text);

private:
    void startCurrentSource();
    void rescanCameras();     // 扫描 /dev/video* 填充设备下拉框
    int  currentDeviceId() const;

    VideoWidget *m_video;
    QComboBox *m_devCombo;
    QComboBox *m_camCombo;    // 自动识别出的摄像头设备列表(/dev/videoN)
    QPushButton *m_btnSim;
    QPushButton *m_btnFfmpeg;
    QPushButton *m_btnCam;
    QLineEdit *m_cmdEdit;
    QPushButton *m_btnApply;
    QPushButton *m_btnSnap;
    QLabel *m_status;
    int m_src = 0;   // 0=模拟 1=FFmpeg 2=本地摄像头
};
