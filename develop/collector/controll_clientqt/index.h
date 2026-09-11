#ifndef INDEX_H
#define INDEX_H

#include <QWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMap>
#include <QMessageBox>
#include <QDateTime>
#include <QPainter>
#include <QFile>
#include <QTextStream>
#include <QtEndian>
#include <QProcess>
#include <QProgressDialog>
#include <QInputDialog>
#include <QDir>

// ============================================================================
// 通信协议定义
// ============================================================================

#define MSG_TYPE_DATA_REPORT  0x01
#define MSG_TYPE_HEARTBEAT    0x02
#define MSG_TYPE_DEVICE_LIST  0x10
#define MSG_TYPE_REGISTER     0x03

#pragma pack(push, 1)

struct ProtocolHeader {
    quint16 magic;
    quint32 payload_len;
    quint8 type;
    quint8 version;
    quint16 device_id;
    quint16 crc16;
};

struct DataPayload {
    quint32 timestamp;
    qint16  temperature;
    quint8  status;
    quint8  humi;
    qint32  production;
    quint8  reserved[3];
};

struct DeviceInfoPayload {
    quint16 device_id;
    char    name[64];
    char    group_name[64];
    quint8  online;
    quint8  reserved[3];
};

#pragma pack(pop)

class Protocol : public QObject
{
    Q_OBJECT
public:
    explicit Protocol(QObject *parent = nullptr);

    bool parsePacket(const QByteArray &data, quint16 &deviceId,
                     QByteArray &payload, quint8 &type);
    QByteArray buildPacket(quint8 type, quint16 deviceId,
                           const QByteArray &payload = QByteArray());
    static quint16 crc16_calc(const QByteArray &data);
};

class Index : public QWidget
{
    Q_OBJECT

public:
    explicit Index(QWidget *parent = nullptr);
    ~Index();

private slots:
    void onConnect();
    void onDisconnect();
    void onStartDevice();
    void onStopDevice();
    void onDeviceSelected(int row);

    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onProcessReadyRead();

    // 新增：用于非阻塞启动的超时检查
    void onStartTimeout();
    void onProcessCheck();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // UI控件
    QLineEdit *m_ipEdit;
    QLineEdit *m_portEdit;
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    QPushButton *m_startDeviceBtn;
    QPushButton *m_stopDeviceBtn;
    QLabel *m_statusIndicator;
    QLabel *m_statusLabel;
    QLabel *m_deviceStatusLabel;
    QListWidget *m_deviceList;
    QLabel *m_deviceInfoLabel;
    QTextEdit *m_logTextEdit;

    // 网络相关
    QTcpSocket *m_socket;
    Protocol m_protocol;
    QByteArray m_recvBuffer;
    QTimer *m_heartbeatTimer;

    // 本地进程相关
    QProcess *m_process;
    QString m_collectorPath;
    QString m_collectorWorkDir;
    bool m_isStartingDevice;
    bool m_isStoppingDevice;
    QProgressDialog *m_progressDialog;

    // 新增：启动超时定时器
    QTimer *m_startTimeoutTimer;
    QTimer *m_processCheckTimer;
    int m_checkCount;
    // 新增：collector 路径输入控件
    QLineEdit *m_collectorPathEdit;    // collector程序路径输入框
    QLineEdit *m_collectorWorkDirEdit; // collector工作目录输入框

    // 状态变量
    bool m_isConnected;
    bool m_isDeviceConnected;
    quint16 m_currentDeviceId;
    QMap<quint16, QString> m_deviceMap;

    struct DeviceData {
        float temperature;
        quint8 humi;
        quint8 status;
        quint32 production;
        time_t timestamp;
        bool hasData;
    };
    QMap<quint16, DeviceData> m_deviceDataMap;

    // 私有方法
    void initUI();
    void initConnections();
    void updateConnectionStatus(bool connected);
    void updateDeviceStatus(bool connected);
    void updateControlPanel(bool enabled);
    void addLog(const QString &msg);
    void sendRegisterPacket();
    void processPacket(quint16 deviceId, const QByteArray &payload, quint8 type);
    void updateDeviceList(quint16 deviceId, const QString &name,
                          const QString &group, bool online);
    void applyStyle();

    void processDataReport(quint16 deviceId, const QByteArray &payload);
    void updateDeviceData(quint16 deviceId, float temperature, quint8 humi,
                          quint8 status, quint32 production);
    void updateDeviceInfoDisplay(quint16 deviceId);
    QListWidgetItem* findDeviceItem(quint16 deviceId);

    void startCollectorLocally();
    void cleanupProcess();
};

#endif // INDEX_H
