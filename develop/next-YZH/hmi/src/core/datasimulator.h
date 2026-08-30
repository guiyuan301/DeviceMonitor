#pragma once
#include <QObject>
#include <QVector>
#include <QTimer>
#include "../datatypes.h"

/*
 * 模拟数据源 (开发/演示用): 1Hz 产生 6 台设备的温度/状态/产量,
 * 带随机游走、偶发高温尖峰、偶发掉线, 便于完整演示告警与离线场景。
 * 接真实数据时整体替换: 服务端解析线程发信号 → DataManager::onDeviceData()。
 */
class DataSimulator : public QObject
{
    Q_OBJECT
public:
    explicit DataSimulator(QObject *parent = nullptr);
    void start(int intervalMs = 1000);
    void stop();
    bool isRunning() const;

signals:
    void deviceData(const DeviceData &data);

private slots:
    void tick();

private:
    struct Sim {
        DeviceData cur;
        double base = 40.0;  // 温度基准
        int spikeLeft = 0;   // 高温尖峰剩余秒数
    };
    QTimer *m_timer;
    QVector<Sim> m_sims;
};
