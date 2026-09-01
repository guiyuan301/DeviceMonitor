#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include "../datatypes.h"

/*
 * 服务端 TCP 客户端 (HMI 侧, 对接队友的 service/central Epoll 服务端)
 *
 * 角色: 中央板大屏作为"看板客户端"连到服务端, 拿到真实采集数据替代模拟器。
 *
 * 工作流程(与 service/central 的协议完全一致):
 *   1. TCP 连上服务端后, 先发一帧 0x03 注册包 —— 声明"我是看板客户端",
 *      服务端(打转发补丁后)只把 0x01 数据上报包转发给注册过的看板;
 *   2. 每 5 秒发一帧 0x02 心跳包(服务端 15 秒收不到心跳会断开连接);
 *   3. 收到服务端转发的 0x01 数据上报包 → 按协议还原温湿度/产量
 *      → emit deviceData() → MainWindow 接给 DataManager 刷大屏;
 *   4. 连接断开(网线拔了/服务端重启)后每 3 秒自动重连, 全程无需人工干预。
 *
 * 协议格式(大端字节序): 12 字节定长包头 + 变长负载
 *   [0-1]魔数0x5A5A [2-5]负载长度 [6]类型 [7]版本 [8-9]设备号 [10-11]CRC16
 *   CRC16 采用 Modbus 标准多项式 0xA001, 对"包头前8字节+负载"计算。
 */
class ServerClient : public QObject
{
    Q_OBJECT
public:
    explicit ServerClient(QObject *parent = nullptr);

    void start(const QString &host, quint16 port);  // 开始连接并保持在线
    void stop();                                    // 停止(退出程序/切回模拟源时调)
    bool isRunning() const { return m_running; }
    bool isConnected() const { return m_sock->state() == QAbstractSocket::ConnectedState; }

signals:
    void connectedChanged(bool on);            // 连接建立/断开(顶栏 LED 用)
    void statusText(const QString &text);      // 状态描述(顶栏"数据源:"文本)
    void deviceData(const DeviceData &data);   // 还原出的一条实时数据 → DataManager

private slots:
    void onConnected();      // 连上: 发注册包 + 启动心跳定时器
    void onDisconnected();   // 断开: 安排重连
    void onReadyRead();      // 有数据到达: 进缓冲区拆包
    void onError(QAbstractSocket::SocketError err); // socket 错误(仅记录状态)
    void sendHeartbeat();    // 定时心跳
    void tryReconnect();     // 定时重连

private:
    void sendPacket(quint8 type, quint16 deviceId, const QByteArray &payload);
    void processBuffer();    // 按"包头+负载"完整帧切分接收缓冲(处理粘包/半包)
    static quint16 crc16(const quint8 *data, int len);  // Modbus CRC16, 与服务端一致

    QTcpSocket *m_sock;
    QTimer *m_hbTimer;       // 5 秒心跳
    QTimer *m_reconnTimer;   // 3 秒重连
    QByteArray m_rbuf;       // 接收缓冲区: 累积字节流, 攒够一整帧才解析
    QString m_host;
    quint16 m_port = 8888;
    bool m_running = false;  // start 后为 true; stop 后不再自动重连
};
