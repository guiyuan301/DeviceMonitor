#include "serverclient.h"
#include <QtEndian>      // qToBigEndian/qFromBigEndian: 主机序↔网络序(大端)转换
#include <QDateTime>

/* 设备负载长度: 时间戳4 + 温度2 + 状态1 + 湿度1 + 产量4 + 保留3 = 15 字节
 * (与 service/central/include/proto.h 的 DataPayload 完全一致) */
static const int kPayloadLen = 15;
static const int kHeaderLen = 12;
static const quint16 kMagic = 0x5A5A;

/* 看板客户端在服务端眼里的"设备号": 取协议最大值 65535 作专用号段,
 * 与真实采集板(1~N)区分开, 方便在服务端日志里认出谁是大屏。 */
static const quint16 kMonitorId = 65535;

ServerClient::ServerClient(QObject *parent) : QObject(parent)
{
    m_sock = new QTcpSocket(this);
    connect(m_sock, &QTcpSocket::connected, this, &ServerClient::onConnected);
    connect(m_sock, &QTcpSocket::disconnected, this, &ServerClient::onDisconnected);
    connect(m_sock, &QTcpSocket::readyRead, this, &ServerClient::onReadyRead);
    connect(m_sock, static_cast<void(QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
            this, &ServerClient::onError);

    // 心跳定时器: 只在连接建立后启动
    m_hbTimer = new QTimer(this);
    m_hbTimer->setInterval(5000);
    connect(m_hbTimer, &QTimer::timeout, this, &ServerClient::sendHeartbeat);

    // 重连定时器: 断线后每 3 秒试一次
    m_reconnTimer = new QTimer(this);
    m_reconnTimer->setInterval(3000);
    connect(m_reconnTimer, &QTimer::timeout, this, &ServerClient::tryReconnect);
}

void ServerClient::start(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_running = true;
    emit statusText(QString("正在连接 %1:%2 ...").arg(host).arg(port));
    m_sock->connectToHost(host, port);   // 异步连接, 结果走 onConnected/onError
}

void ServerClient::stop()
{
    m_running = false;                   // 先关开关, 断开后不再自动重连
    m_hbTimer->stop();
    m_reconnTimer->stop();
    if (m_sock->state() != QAbstractSocket::UnconnectedState)
        m_sock->abort();                 // abort: 立即断开并丢弃缓冲
    m_rbuf.clear();
}

/* ================= 连接生命周期 ================= */

void ServerClient::onConnected()
{
    m_reconnTimer->stop();
    m_hbTimer->start();

    /* 注册包: 类型 0x03、负载为空。
     * 服务端打上转发补丁后, 收到 0x03 就把这个连接标记为"看板",
     * 之后每解析出一帧 0x01 数据都会转发给所有看板。 */
    sendPacket(0x03, kMonitorId, QByteArray());

    // 立刻补一帧心跳, 让服务端马上把本连接置为在线
    sendHeartbeat();
    emit connectedChanged(true);
    emit statusText(QString("服务端 %1:%2 已连接").arg(m_host).arg(m_port));
}

void ServerClient::onDisconnected()
{
    m_hbTimer->stop();
    emit connectedChanged(false);
    if (!m_running)
        return;
    emit statusText(QString("与服务端断开, %1 秒后重连...").arg(m_reconnTimer->interval() / 1000));
    m_reconnTimer->start();
}

void ServerClient::onError(QAbstractSocket::SocketError err)
{
    // 常见: ConnectionRefused(服务端没开)/HostNotFound(IP 不对)/NetworkError(拔网线)
    emit statusText(QString("连接错误(%1), 等待重连...").arg(int(err)));
    // 注意: error 后通常会跟着 disconnected, 重连逻辑集中在那里做
}

void ServerClient::tryReconnect()
{
    if (!m_running || m_sock->state() != QAbstractSocket::UnconnectedState)
        return;
    emit statusText(QString("重连 %1:%2 ...").arg(m_host).arg(m_port));
    m_sock->connectToHost(m_host, m_port);
}

void ServerClient::sendHeartbeat()
{
    if (!isConnected())
        return;
    sendPacket(0x02, kMonitorId, QByteArray());   // 心跳: 类型 0x02, 负载为空
}

/* ================= 发包: 组装 12 字节包头 + 负载 ================= */

void ServerClient::sendPacket(quint8 type, quint16 deviceId, const QByteArray &payload)
{
    QByteArray pkt(kHeaderLen, Qt::Uninitialized);
    quint8 *h = reinterpret_cast<quint8 *>(pkt.data());

    // [0-1] 魔数 0x5A5A。0x5A 两个字节对称, 大小端在 wire 上都是 5A 5A
    qToBigEndian<quint16>(kMagic, h);
    // [2-5] 负载长度(不含包头), 网络字节序 = 大端
    qToBigEndian<quint32>(quint32(payload.size()), h + 2);
    // [6] 报文类型  [7] 协议版本 = 1
    h[6] = type;
    h[7] = 0x01;
    // [8-9] 设备号
    qToBigEndian<quint16>(deviceId, h + 8);
    // [10-11] CRC 先占位 0, 下面算完再回填
    qToBigEndian<quint16>(quint16(0), h + 10);

    /* CRC16 计算范围 = 包头前 8 字节 + 负载(即跳过设备号和 CRC 自身)。
     * CRC 必须按"线上字节"计算, 所以先拼好前 8 字节 + 负载的临时缓冲。 */
    QByteArray tmp = pkt.left(8) + payload;
    const quint16 crc = crc16(reinterpret_cast<const quint8 *>(tmp.constData()), tmp.size());
    qToBigEndian<quint16>(crc, h + 10);

    pkt.append(payload);
    m_sock->write(pkt);
}

/* Modbus 标准 CRC16: 初值 0xFFFF, 多项式 0xA001(即 0x8005 反转)。
 * 必须与服务端 protocol_parser.c 里的 crc16_calc 完全一致, 否则包全被丢。 */
quint16 ServerClient::crc16(const quint8 *data, int len)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* ================= 收包: 缓冲区拆帧 + 字段还原 ================= */

void ServerClient::onReadyRead()
{
    // QTcpSocket 是"字节流"没有消息边界: 一次 read 可能读到半个包,
    // 也可能读到好几个粘连的包, 所以统一丢进 m_rbuf 攒着慢慢切
    m_rbuf += m_sock->readAll();
    if (m_rbuf.size() > 64 * 1024)       // 防御: 异常洪流时不让内存涨爆
        m_rbuf.clear();
    processBuffer();
}

void ServerClient::processBuffer()
{
    forever {
        // ① 不够一个包头, 等下一次数据
        if (m_rbuf.size() < kHeaderLen)
            break;

        const quint8 *h = reinterpret_cast<const quint8 *>(m_rbuf.constData());

        // ② 魔数不对 → 流里混了脏数据, 丢 1 字节重新对齐(与服务端同款恢复策略)
        if (qFromBigEndian<quint16>(h) != kMagic) {
            m_rbuf.remove(0, 1);
            continue;
        }

        // ③ 负载长度合法性: 协议里最大 1024, 超了按错位处理
        const quint32 payloadLen = qFromBigEndian<quint32>(h + 2);
        if (payloadLen > 1024) {
            m_rbuf.remove(0, 1);
            continue;
        }

        // ④ 整帧还没到齐(半包), 退出等下一次 readyRead
        const int total = kHeaderLen + int(payloadLen);
        if (m_rbuf.size() < total)
            break;

        const quint8 type = h[6];
        const quint16 deviceId = qFromBigEndian<quint16>(h + 8);
        const quint8 *payload = reinterpret_cast<const quint8 *>(h) + kHeaderLen;

        if (type == 0x01 && int(payloadLen) == kPayloadLen) {
            /* 数据上报帧(服务端转发来的真实采集数据), 逐字段按大端还原:
             *   [0-3]时间戳(秒) [4-5]温度×100 [6]状态 [7]湿度 [8-11]产量 [12-14]保留 */
            DeviceData d;
            d.id = int(deviceId);
            d.temp = qint16(qFromBigEndian<quint16>(payload + 4)) / 100.0;  // 2536 → 25.36℃
            d.humi = payload[7];                                            // 湿度整数%
            d.output = qFromBigEndian<quint32>(payload + 8);                // 累计产量
            d.online = true;
            d.ts = QDateTime::currentMSecsSinceEpoch();  // 用本机毫秒时间, 与历史曲线对齐
            emit deviceData(d);
        }
        // 0x02/0x03/其它类型: 客户端只需"消费"掉即可, 无需处理

        // ⑤ 帧已处理, 从缓冲区头部移除, 继续看剩余字节里还有没有完整帧
        m_rbuf.remove(0, total);
    }
}
