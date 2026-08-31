#pragma once
#include <QObject>
#include <QVector>
#include <QTimer>
#include "../datatypes.h"

/*
 * 模拟数据源 V3 (开发/演示用): 1Hz 产生 3 个采集点的温湿度/产量(红外计数),
 * 含随机游走、温湿度尖峰、偶发掉线、产线匀速出件;
 * 收到 snapshotRequested 时生成对应主题的模拟抓拍图。
 * 接真实数据时整体替换: 服务端解析线程 → DataManager 接口(见项目文档)。
 */
class DataSimulator : public QObject
{
    Q_OBJECT
public:
    explicit DataSimulator(QObject *parent = nullptr);
    void start(int intervalMs = 1000);
    void stop();
    bool isRunning() const;

public slots:
    // 收到抓拍请求: 生成对应主题的模拟抓拍图并回存 DataManager
    void onSnapshotRequested(int deviceId, const QString &reason);

signals:
    void deviceData(const DeviceData &data);

private slots:
    void tick();

private:
    struct Sim {
        DeviceData cur;
        double tBase = 28.0;   // 温度基准
        double hBase = 55.0;   // 湿度基准
        int tSpikeLeft = 0;    // 高温尖峰剩余秒数
        int hSpikeLeft = 0;    // 高湿尖峰剩余秒数
    };
    QString makeSnapshot(const DeviceData &d, const QString &reason);

    QTimer *m_timer;
    QVector<Sim> m_sims;
};
