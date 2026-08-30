#include "videowidget.h"
#include "../theme.h"
#include <QPainter>
#include <QDateTime>
#include <QtGlobal>

VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(160, 120);
    m_simTimer = new QTimer(this);
    m_simTimer->setInterval(100);          // 模拟画面 10fps
    connect(m_simTimer, &QTimer::timeout, this, &VideoWidget::onSimTick);
}

VideoWidget::~VideoWidget()
{
    stopSource();
}

void VideoWidget::setSource(Source src, const QString &ffmpegCmd)
{
    stopSource();
    m_src = src;
    m_errorMsg.clear();
    m_buf.clear();
    m_frames = 0;
    m_fps = 0;
    m_windowStart = QDateTime::currentMSecsSinceEpoch();

    if (m_src == SimSource) {
        m_simTimer->start();
        onSimTick();
    } else {
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::SeparateChannels); // stderr 不污染视频流
        connect(m_proc, &QProcess::readyReadStandardOutput,
                this, &VideoWidget::onReadyRead);
        connect(m_proc, static_cast<void(QProcess::*)(QProcess::ProcessError)>(&QProcess::errorOccurred),
                this, &VideoWidget::onProcError);
        connect(m_proc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, &VideoWidget::onProcFinished);
        m_proc->start(ffmpegCmd);
    }
    emit statusChanged(statusText());
}

void VideoWidget::stopSource()
{
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
    QString src = (m_src == SimSource) ? "模拟画面" : "FFmpeg";
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

void VideoWidget::onProcError(QProcess::ProcessError err)
{
    if (err == QProcess::FailedToStart)
        m_errorMsg = "ffmpeg 启动失败(未安装或命令错误)";
    else if (err == QProcess::Crashed)
        m_errorMsg = "ffmpeg 异常退出";
    else
        m_errorMsg = QString("ffmpeg 错误(%1)").arg(int(err));
    emit sourceMessage(m_errorMsg);
    emit statusChanged(statusText());
    update();
}

void VideoWidget::onProcFinished(int, QProcess::ExitStatus st)
{
    if (m_src != FfmpegSource)
        return;
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
