#pragma once
#include <QWidget>

class QComboBox;
class QPushButton;
class QLabel;
class QLineEdit;
class VideoWidget;

/*
 * 视频监控页 V3: 实时画面(FFmpeg拉流/模拟双源) + 手动抓拍。
 * ffmpeg 命令与来源选择用 QSettings 持久化。
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
    void onApplyCmd();
    void onSnap();
    void onStatus(const QString &text);

private:
    void startCurrentSource();
    int  currentDeviceId() const;

    VideoWidget *m_video;
    QComboBox *m_devCombo;
    QPushButton *m_btnSim;
    QPushButton *m_btnFfmpeg;
    QLineEdit *m_cmdEdit;
    QPushButton *m_btnApply;
    QPushButton *m_btnSnap;
    QLabel *m_status;
    int m_src = 0;   // 0=模拟 1=FFmpeg
};
