#include "index.h"
#include <QMessageBox>
#include <QDateTime>
#include <QPainter>
#include <QFile>
#include <QTextStream>
#include <QtEndian>
#include <QInputDialog>
#include <QProgressDialog>
#include <QFileInfo>
#include <QProcess>
#include <QDir>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <QCoreApplication>

// ============================================================================
// Protocol 协议处理类
// ============================================================================

Protocol::Protocol(QObject *parent) : QObject(parent)
{
}

quint16 Protocol::crc16_calc(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= (quint8)data.at(i);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

QByteArray Protocol::buildPacket(quint8 type, quint16 deviceId,
                                  const QByteArray &payload)
{
    ProtocolHeader hdr;
    hdr.magic = qToBigEndian((quint16)0x5A5A);
    hdr.payload_len = qToBigEndian((quint32)payload.size());
    hdr.type = type;
    hdr.version = 1;
    hdr.device_id = qToBigEndian(deviceId);
    hdr.crc16 = 0;

    QByteArray packet;
    packet.append((char*)&hdr, sizeof(ProtocolHeader));
    if (!payload.isEmpty()) {
        packet.append(payload);
    }

    QByteArray crcData = packet.left(8);
    crcData.append(payload);
    quint16 crc = crc16_calc(crcData);
    crc = qToBigEndian(crc);
    packet.replace(10, 2, (char*)&crc, 2);

    return packet;
}

bool Protocol::parsePacket(const QByteArray &data, quint16 &deviceId,
                           QByteArray &payload, quint8 &type)
{
    if (data.size() < (int)sizeof(ProtocolHeader)) {
        return false;
    }

    int startPos = -1;
    for (int i = 0; i <= data.size() - 2; ++i) {
        quint16 magic;
        memcpy(&magic, data.data() + i, 2);
        if (qFromBigEndian(magic) == 0x5A5A) {
            startPos = i;
            break;
        }
    }

    if (startPos == -1 || startPos > 0) {
        return false;
    }

    ProtocolHeader hdr;
    memcpy(&hdr, data.data(), sizeof(ProtocolHeader));

    quint32 payloadLen = qFromBigEndian(hdr.payload_len);
    int totalLen = sizeof(ProtocolHeader) + payloadLen;

    if (data.size() < totalLen) {
        return false;
    }

    QByteArray crcData = data.left(8);
    crcData.append(data.mid(sizeof(ProtocolHeader), payloadLen));
    quint16 calcCrc = crc16_calc(crcData);
    quint16 pktCrc = qFromBigEndian(hdr.crc16);

    if (calcCrc != pktCrc) {
        return false;
    }

    if (payloadLen > 0) {
        payload = data.mid(sizeof(ProtocolHeader), payloadLen);
    } else {
        payload.clear();
    }

    deviceId = qFromBigEndian(hdr.device_id);
    type = hdr.type;

    return true;
}

// ============================================================================
// Index 主窗口实现
// ============================================================================

Index::Index(QWidget *parent)
    : QWidget(parent)
    , m_socket(new QTcpSocket(this))
    , m_heartbeatTimer(new QTimer(this))
    , m_process(nullptr)
    , m_isConnected(false)
    , m_isDeviceConnected(false)
    , m_isStartingDevice(false)
    , m_isStoppingDevice(false)
    , m_currentDeviceId(0)
    , m_progressDialog(nullptr)
    , m_startTimeoutTimer(new QTimer(this))
    , m_processCheckTimer(new QTimer(this))
    , m_checkCount(0)
    , m_collectorPath("/mnt/nfs/collector")
    , m_collectorWorkDir("/mnt/nfs")
    , m_collectorPathEdit(nullptr)      // 新增初始化
    , m_collectorWorkDirEdit(nullptr)   // 新增初始化
{
    initUI();
    initConnections();
    applyStyle();

    setWindowTitle("采集板控制中心");
    setFixedSize(480, 272);

    // ================================================================
    // 从输入框读取初始路径
    // ================================================================
    m_collectorPath = m_collectorPathEdit->text().trimmed();
    m_collectorWorkDir = m_collectorWorkDirEdit->text().trimmed();

    updateConnectionStatus(false);
    updateDeviceStatus(false);
    updateControlPanel(false);
    addLog("程序启动，请连接服务器");
}

Index::~Index()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
    cleanupProcess();
    if (m_progressDialog) {
        delete m_progressDialog;
    }
}

void Index::cleanupProcess()
{
    if (m_process) {
        if (m_process->state() == QProcess::Running) {
            qint64 pid = m_process->processId();
            if (pid > 0) {
                kill(pid, SIGINT);
                m_process->waitForFinished(2000);
            }
            if (m_process->state() == QProcess::Running) {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_isDeviceConnected = false;
    m_isStartingDevice = false;
    m_isStoppingDevice = false;
}

// ============================================================================
// UI 初始化
// ============================================================================

void Index::initUI()
{
    // ===== 顶部：连接区域 =====
    QLabel *ipLabel = new QLabel("服务IP:", this);
    ipLabel->setObjectName("titleLabel");

    m_ipEdit = new QLineEdit("192.168.14.50", this);
    m_ipEdit->setObjectName("inputEdit");
    m_ipEdit->setMinimumWidth(90);
    m_ipEdit->setPlaceholderText("服务端IP");

    QLabel *portLabel = new QLabel("端口:", this);
    portLabel->setObjectName("titleLabel");

    m_portEdit = new QLineEdit("8888", this);
    m_portEdit->setObjectName("inputEdit");
    m_portEdit->setMinimumWidth(45);
    m_portEdit->setPlaceholderText("端口");

    m_connectBtn = new QPushButton("连接", this);
    m_connectBtn->setObjectName("connectBtn");
    m_connectBtn->setFixedSize(46, 26);

    m_disconnectBtn = new QPushButton("断开", this);
    m_disconnectBtn->setObjectName("disconnectBtn");
    m_disconnectBtn->setFixedSize(46, 26);
    m_disconnectBtn->setEnabled(false);

    m_statusIndicator = new QLabel(this);
    m_statusIndicator->setObjectName("statusIndicator");
    m_statusIndicator->setFixedSize(12, 12);

    m_statusLabel = new QLabel("未连接", this);
    m_statusLabel->setObjectName("statusLabel");

    m_deviceStatusLabel = new QLabel("设备离线", this);
    m_deviceStatusLabel->setObjectName("deviceStatusLabel");

    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setSpacing(3);
    topLayout->addWidget(ipLabel);
    topLayout->addWidget(m_ipEdit);
    topLayout->addWidget(portLabel);
    topLayout->addWidget(m_portEdit);
    topLayout->addWidget(m_connectBtn);
    topLayout->addWidget(m_disconnectBtn);
    topLayout->addWidget(m_statusIndicator);
    topLayout->addWidget(m_statusLabel);
    topLayout->addWidget(m_deviceStatusLabel);
    topLayout->addStretch();

    // ===== 中间：设备列表 + 控制面板 =====
    m_deviceList = new QListWidget(this);
    m_deviceList->setObjectName("deviceList");
    m_deviceList->setFixedWidth(130);
    m_deviceList->setToolTip("点击选中设备查看详细信息");

    // ================================================================
    // 右侧面板：设备信息 + Collector路径配置 + 启动/停止按钮
    // ================================================================

    // 设备信息标签
    m_deviceInfoLabel = new QLabel("请选择设备", this);
    m_deviceInfoLabel->setObjectName("deviceInfoLabel");
    m_deviceInfoLabel->setAlignment(Qt::AlignCenter);
    m_deviceInfoLabel->setFixedHeight(30);
    m_deviceInfoLabel->setMinimumWidth(200);

    // ================================================================
    // 新增：Collector路径配置区域（使用 QLineEdit 输入）
    // ================================================================
    QLabel *pathLabel = new QLabel("程序路径:", this);
    pathLabel->setObjectName("titleLabel");
    pathLabel->setFixedWidth(55);

    m_collectorPathEdit = new QLineEdit("/mnt/nfs/collector", this);
    m_collectorPathEdit->setObjectName("inputEdit");
    m_collectorPathEdit->setPlaceholderText("collector程序路径");
    m_collectorPathEdit->setMinimumWidth(150);

    QLabel *workDirLabel = new QLabel("工作目录:", this);
    workDirLabel->setObjectName("titleLabel");
    workDirLabel->setFixedWidth(55);

    m_collectorWorkDirEdit = new QLineEdit("/mnt/nfs", this);
    m_collectorWorkDirEdit->setObjectName("inputEdit");
    m_collectorWorkDirEdit->setPlaceholderText("工作目录");
    m_collectorWorkDirEdit->setMinimumWidth(150);

    // 路径输入区域布局（两行）
    QVBoxLayout *pathLayout = new QVBoxLayout();
    pathLayout->setSpacing(4);

    QHBoxLayout *pathRow1 = new QHBoxLayout();
    pathRow1->setSpacing(4);
    pathRow1->addWidget(pathLabel);
    pathRow1->addWidget(m_collectorPathEdit);

    QHBoxLayout *pathRow2 = new QHBoxLayout();
    pathRow2->setSpacing(4);
    pathRow2->addWidget(workDirLabel);
    pathRow2->addWidget(m_collectorWorkDirEdit);

    pathLayout->addLayout(pathRow1);
    pathLayout->addLayout(pathRow2);

    // ===== 启动/停止按钮 =====
    m_startDeviceBtn = new QPushButton("启动采集", this);
    m_startDeviceBtn->setObjectName("startDeviceBtn");
    m_startDeviceBtn->setFixedSize(120, 50);
    m_startDeviceBtn->setToolTip("本地启动collector采集程序");
    m_startDeviceBtn->setEnabled(false);

    m_stopDeviceBtn = new QPushButton("停止采集", this);
    m_stopDeviceBtn->setObjectName("stopDeviceBtn");
    m_stopDeviceBtn->setFixedSize(120, 50);
    m_stopDeviceBtn->setToolTip("停止collector程序（发送SIGINT信号优雅退出）");
    m_stopDeviceBtn->setEnabled(false);

    // 启动/停止按钮水平布局 - 居中
    QHBoxLayout *deviceControlLayout = new QHBoxLayout();
    deviceControlLayout->setSpacing(30);
    deviceControlLayout->addStretch();
    deviceControlLayout->addWidget(m_startDeviceBtn);
    deviceControlLayout->addWidget(m_stopDeviceBtn);
    deviceControlLayout->addStretch();

    // ===== 右侧面板布局 =====
    // 从上到下：设备信息 -> Collector路径 -> 启动/停止按钮
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(6);
    rightLayout->addWidget(m_deviceInfoLabel);
    rightLayout->addSpacing(4);
    rightLayout->addLayout(pathLayout);
    rightLayout->addSpacing(6);
    rightLayout->addLayout(deviceControlLayout);
    rightLayout->addStretch();

    // 中间水平布局
    QHBoxLayout *middleLayout = new QHBoxLayout();
    middleLayout->setSpacing(8);
    middleLayout->addWidget(m_deviceList);
    middleLayout->addLayout(rightLayout);

    // ===== 底部：日志输出 =====
    m_logTextEdit = new QTextEdit(this);
    m_logTextEdit->setObjectName("logTextEdit");
    m_logTextEdit->setMaximumHeight(55);
    m_logTextEdit->setReadOnly(true);
    m_logTextEdit->setLineWrapMode(QTextEdit::NoWrap);

    // ===== 整体垂直布局 =====
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 4, 6, 4);
    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(middleLayout);
    mainLayout->addWidget(m_logTextEdit);
}

void Index::initConnections()
{
    connect(m_connectBtn, &QPushButton::clicked, this, &Index::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &Index::onDisconnect);
    connect(m_startDeviceBtn, &QPushButton::clicked, this, &Index::onStartDevice);
    connect(m_stopDeviceBtn, &QPushButton::clicked, this, &Index::onStopDevice);

    connect(m_deviceList, &QListWidget::itemClicked,
            [this](QListWidgetItem *item) {
                onDeviceSelected(m_deviceList->row(item));
            });

    connect(m_socket, &QTcpSocket::connected, this, &Index::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &Index::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &Index::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &Index::onSocketError);

    connect(m_heartbeatTimer, &QTimer::timeout, [this]() {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            QByteArray heartbeat = m_protocol.buildPacket(MSG_TYPE_HEARTBEAT, 0);
            m_socket->write(heartbeat);
        }
    });
    m_heartbeatTimer->setInterval(10000);

    // ===== 非阻塞启动相关定时器 =====
    m_startTimeoutTimer->setSingleShot(true);
    connect(m_startTimeoutTimer, &QTimer::timeout, this, &Index::onStartTimeout);

    connect(m_processCheckTimer, &QTimer::timeout, this, &Index::onProcessCheck);
    m_processCheckTimer->setInterval(500);
}

void Index::applyStyle()
{
    QString style =
        "Index { background-color: #1e1e2e; }"
        "QWidget { background-color: #1e1e2e; }"

        "QLabel#titleLabel { color: #cdd6f4; font-size: 11px; }"
        "QLabel#statusLabel { color: #cdd6f4; font-size: 11px; }"
        "QLabel#deviceStatusLabel { color: #f38ba8; font-size: 11px; }"
        "QLabel#deviceInfoLabel {"
        "    color: #89b4fa; font-size: 14px; font-weight: bold;"
        "    padding: 6px; background: #313244; border-radius: 6px;"
        "}"
        "QLabel#statusIndicator {"
        "    border-radius: 6px; background: #f38ba8;"
        "}"

        "QLineEdit#inputEdit {"
        "    background-color: #313244; color: #cdd6f4;"
        "    border: 1px solid #45475a; border-radius: 4px;"
        "    padding: 3px 5px; font-size: 11px;"
        "}"
        "QLineEdit#inputEdit:focus { border-color: #89b4fa; }"

        "QPushButton {"
        "    background-color: #45475a; color: #cdd6f4;"
        "    border: none; border-radius: 4px;"
        "    padding: 3px 6px; font-size: 11px; font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #585b70; }"
        "QPushButton:pressed { background-color: #313244; }"
        "QPushButton:disabled { background-color: #313244; color: #6c7086; }"

        "QPushButton#connectBtn {"
        "    background-color: #a6e3a1; color: #1e1e2e;"
        "}"
        "QPushButton#connectBtn:hover { background-color: #94e2d5; }"
        "QPushButton#connectBtn:disabled { background-color: #45475a; color: #6c7086; }"

        "QPushButton#disconnectBtn {"
        "    background-color: #f38ba8; color: #1e1e2e;"
        "}"
        "QPushButton#disconnectBtn:hover { background-color: #eba0ac; }"
        "QPushButton#disconnectBtn:disabled { background-color: #45475a; color: #6c7086; }"

        "QPushButton#startDeviceBtn {"
        "    background-color: #89b4fa; color: #1e1e2e; font-size: 18px; font-weight: bold;"
        "    border-radius: 8px;"
        "}"
        "QPushButton#startDeviceBtn:hover { background-color: #74c7ec; }"
        "QPushButton#startDeviceBtn:disabled { background-color: #313244; color: #6c7086; }"

        "QPushButton#stopDeviceBtn {"
        "    background-color: #f38ba8; color: #1e1e2e; font-size: 18px; font-weight: bold;"
        "    border-radius: 8px;"
        "}"
        "QPushButton#stopDeviceBtn:hover { background-color: #eba0ac; }"
        "QPushButton#stopDeviceBtn:disabled { background-color: #313244; color: #6c7086; }"

        "QListWidget#deviceList {"
        "    background-color: #313244; color: #cdd6f4;"
        "    border: 1px solid #45475a; border-radius: 6px;"
        "    font-size: 11px; padding: 4px;"
        "}"
        "QListWidget#deviceList::item { padding: 4px 8px; border-radius: 4px; }"
        "QListWidget#deviceList::item:selected {"
        "    background-color: #89b4fa; color: #1e1e2e;"
        "}"
        "QListWidget#deviceList::item:hover { background-color: #45475a; }"

        "QTextEdit#logTextEdit {"
        "    background-color: #181825; color: #cdd6f4;"
        "    border: 1px solid #45475a; border-radius: 4px;"
        "    font-family: monospace; font-size: 9px;"
        "}";

    setStyleSheet(style);
}

void Index::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setPen(QPen(QColor("#45475a"), 1));
    painter.drawLine(0, 34, width(), 34);
}

// ============================================================================
// 按钮槽函数
// ============================================================================

void Index::onConnect()
{
    QString ip = m_ipEdit->text().trimmed();
    quint16 port = m_portEdit->text().toUShort();

    if (ip.isEmpty() || port == 0) {
        QMessageBox::warning(this, "输入错误", "请输入有效的 IP 地址和端口号");
        return;
    }

    addLog(QString("正在连接 %1:%2...").arg(ip).arg(port));
    m_socket->connectToHost(ip, port);
}

void Index::onDisconnect()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
        addLog("正在断开连接...");
    }
}

// ============================================================================
// 设备启动/停止（核心修复）
// ============================================================================

/**
 * @brief 点击"启动"按钮
 * 完全非阻塞方式启动
 */
void Index::onStartDevice()
{
    if (m_isStartingDevice) {
        addLog("正在启动设备，请稍候...");
        return;
    }

    // ================================================================
    // 修改：从输入框读取路径，不再弹出 QInputDialog
    // ================================================================
    QString path = m_collectorPathEdit->text().trimmed();
    QString workDir = m_collectorWorkDirEdit->text().trimmed();

    // 验证输入
    if (path.isEmpty()) {
        addLog("[错误] 程序路径不能为空");
        QMessageBox::warning(this, "输入错误", "请输入 collector 程序路径");
        return;
    }

    if (workDir.isEmpty()) {
        addLog("[错误] 工作目录不能为空");
        QMessageBox::warning(this, "输入错误", "请输入工作目录");
        return;
    }

    // 更新成员变量
    m_collectorPath = path;
    m_collectorWorkDir = workDir;

    addLog("程序路径: " + m_collectorPath);
    addLog("工作目录: " + m_collectorWorkDir);

    // 延迟启动，让UI刷新
    QTimer::singleShot(100, this, &Index::startCollectorLocally);
}

/**
 * @brief 非阻塞启动 collector
 * 使用定时器轮询检查进程状态，完全避免阻塞UI
 */
void Index::startCollectorLocally()
{
    if (m_isStartingDevice) {
        return;
    }

    // ================================================================
    // 从输入框读取最新路径
    // ================================================================
    m_collectorPath = m_collectorPathEdit->text().trimmed();
    m_collectorWorkDir = m_collectorWorkDirEdit->text().trimmed();

    // 检查程序是否存在
    QFileInfo fileInfo(m_collectorPath);
    if (!fileInfo.exists()) {
        addLog("[错误] 程序不存在: " + m_collectorPath);
        addLog("[提示] 请确认路径是否正确");
        return;
    }
    if (!fileInfo.isExecutable()) {
        addLog("[错误] 程序没有执行权限: " + m_collectorPath);
        addLog("[提示] 执行: chmod +x " + m_collectorPath);
        return;
    }

    // 检查工作目录是否存在
    QDir workDir(m_collectorWorkDir);
    if (!workDir.exists()) {
        addLog("[错误] 工作目录不存在: " + m_collectorWorkDir);
        addLog("[提示] 请确认 NFS 挂载点是否正确");
        return;
    }

    // 检查 collector.conf 是否在工作目录中（collector 使用相对路径 ./collector.conf）
    QString confPath = m_collectorWorkDir + "/collector.conf";
    if (!QFile::exists(confPath)) {
        addLog("[错误] 配置文件不存在: " + confPath);
        addLog("[提示] collector 读取 ./collector.conf，需将其放到工作目录");
        addLog(QString("[提示] 执行: cp collector.conf %1/").arg(m_collectorWorkDir));
        QMessageBox::warning(this, "配置缺失",
            QString("工作目录 %1 下缺少 collector.conf，collector 将无法启动。\n请先复制配置文件到该目录。")
                .arg(m_collectorWorkDir));
        return;
    }

    // 如果已有进程在运行，先停止
    if (m_process && m_process->state() == QProcess::Running) {
        addLog("collector 已在运行，先停止...");
        qint64 pid = m_process->processId();
        if (pid > 0) {
            kill(pid, SIGINT);
            m_process->waitForFinished(2000);
        }
        if (m_process->state() == QProcess::Running) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_isStartingDevice = true;
    m_startDeviceBtn->setEnabled(false);
    m_startDeviceBtn->setText("启动中");

    // 创建进度对话框
    m_progressDialog = new QProgressDialog("正在启动 collector...", "取消", 0, 0, this);
    m_progressDialog->setWindowTitle("启动设备");
    m_progressDialog->setCancelButton(nullptr);
    m_progressDialog->setModal(true);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->show();
    QCoreApplication::processEvents();

    addLog("[调试] 程序路径: " + m_collectorPath);
    addLog("[调试] 工作目录: " + m_collectorWorkDir);

    // 创建并启动进程
    m_process = new QProcess(this);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Index::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &Index::onProcessError);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &Index::onProcessReadyRead);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &Index::onProcessReadyRead);

    m_process->setWorkingDirectory(m_collectorWorkDir);

    // 传递配置文件绝对路径作为参数，避免相对路径歧义
    QString confArg = m_collectorWorkDir + "/collector.conf";
    QStringList args;
    args << confArg;

    addLog("正在启动 collector...");
    m_process->start(m_collectorPath, args);

    // ================================================================
    // 完全非阻塞：启动定时器检查进程是否启动成功
    // 不调用 waitForStarted()，完全避免阻塞
    // ================================================================
    m_checkCount = 0;
    m_processCheckTimer->start(500);
    m_startTimeoutTimer->start(10000);  // 10秒超时
}

/**
 * @brief 定时检查进程启动状态（非阻塞）
 */
void Index::onProcessCheck()
{
    m_checkCount++;

    if (!m_process) {
        m_processCheckTimer->stop();
        m_startTimeoutTimer->stop();
        onStartTimeout();
        return;
    }

    QProcess::ProcessState state = m_process->state();

    if (state == QProcess::Running) {
        // 进程已启动成功
        m_processCheckTimer->stop();
        m_startTimeoutTimer->stop();

        if (m_progressDialog) {
            m_progressDialog->close();
            delete m_progressDialog;
            m_progressDialog = nullptr;
        }

        addLog("collector 进程已启动，PID: " + QString::number(m_process->processId()));
        addLog("等待 collector 连接 TCP 服务端...");

        m_stopDeviceBtn->setEnabled(true);

        // 延迟检查设备连接状态
        QTimer::singleShot(3000, this, [this]() {
            if (m_process && m_process->state() == QProcess::Running) {
                addLog("collector 进程运行正常");
                m_isDeviceConnected = true;
                updateDeviceStatus(true);
                m_stopDeviceBtn->setEnabled(true);
                if (m_currentDeviceId > 0) {
                    updateControlPanel(true);
                }
                addLog("设备已就绪");
            } else {
                addLog("[警告] collector 进程可能已退出");
                addLog("[提示] 查看日志: cat /tmp/collector.log");
                m_isDeviceConnected = false;
                updateDeviceStatus(false);
                m_stopDeviceBtn->setEnabled(false);
            }
            m_isStartingDevice = false;
            m_startDeviceBtn->setEnabled(true);
            m_startDeviceBtn->setText("启动");
        });

        m_isStartingDevice = false;
        m_startDeviceBtn->setEnabled(true);
        m_startDeviceBtn->setText("启动");
        return;
    }

    if (state == QProcess::NotRunning) {
        // 进程已退出（可能是启动失败）
        m_processCheckTimer->stop();
        m_startTimeoutTimer->stop();

        QString errorMsg = m_process->errorString();
        addLog("[错误] collector 启动失败: " + errorMsg);
        addLog("[提示] 请检查程序是否可执行，或查看日志: cat /tmp/collector.log");

        if (m_progressDialog) {
            m_progressDialog->close();
            delete m_progressDialog;
            m_progressDialog = nullptr;
        }

        m_process->deleteLater();
        m_process = nullptr;

        m_isStartingDevice = false;
        m_startDeviceBtn->setEnabled(true);
        m_startDeviceBtn->setText("启动");
        updateDeviceStatus(false);
        return;
    }

    // 还在 Starting 状态，继续等待
    addLog(QString("等待进程启动... (%1/20)").arg(m_checkCount));
}

/**
 * @brief 启动超时处理
 */
void Index::onStartTimeout()
{
    m_processCheckTimer->stop();

    if (m_progressDialog) {
        m_progressDialog->close();
        delete m_progressDialog;
        m_progressDialog = nullptr;
    }

    if (m_process && m_process->state() == QProcess::Starting) {
        addLog("[错误] 进程启动超时（10秒）");
        addLog("[提示] 请检查程序路径是否正确");
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_isStartingDevice = false;
    m_startDeviceBtn->setEnabled(true);
    m_startDeviceBtn->setText("启动");
    updateDeviceStatus(false);
}

/**
 * @brief 点击"停止"按钮
 */
void Index::onStopDevice()
{
    if (m_isStoppingDevice) {
        addLog("正在停止设备，请稍候...");
        return;
    }

    if (!m_process) {
        addLog("collector 进程不存在");
        m_isDeviceConnected = false;
        updateDeviceStatus(false);
        m_stopDeviceBtn->setEnabled(false);
        return;
    }

    if (m_process->state() != QProcess::Running) {
        addLog("collector 进程未运行");
        m_isDeviceConnected = false;
        updateDeviceStatus(false);
        m_stopDeviceBtn->setEnabled(false);
        m_process->deleteLater();
        m_process = nullptr;
        return;
    }

    m_isStoppingDevice = true;
    m_stopDeviceBtn->setEnabled(false);
    m_stopDeviceBtn->setText("停止中");

    addLog("正在停止 collector 进程（发送 SIGINT 信号）...");

    qint64 pid = m_process->processId();
    if (pid > 0) {
        addLog(QString("发送 SIGINT 信号到进程 PID: %1").arg(pid));
        if (kill(pid, SIGINT) == 0) {
            addLog("SIGINT 信号已发送，等待程序清理资源...");
        } else {
            addLog("[警告] 发送 SIGINT 信号失败，尝试使用 terminate()");
            m_process->terminate();
        }
    } else {
        addLog("[警告] 无法获取进程 PID，使用 terminate()");
        m_process->terminate();
    }

    // 非阻塞方式：依赖 finished 信号（onProcessFinished）完成清理
    // 设置5秒兜底定时器，超时则强杀进程
    QTimer::singleShot(5000, this, [this]() {
        if (m_process && m_process->state() == QProcess::Running) {
            addLog("进程未响应 SIGINT，强制终止...");
            m_process->kill();
        }
    });
}

// ============================================================================
// 网络事件处理
// ============================================================================

void Index::onConnected()
{
    m_isConnected = true;
    updateConnectionStatus(true);
    addLog("TCP 连接已建立");

    sendRegisterPacket();
    addLog("HMI 注册包已发送");

    m_heartbeatTimer->start();

    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(true);
    m_startDeviceBtn->setEnabled(true);
    m_stopDeviceBtn->setEnabled(false);
}

void Index::onDisconnected()
{
    m_isConnected = false;
    updateConnectionStatus(false);
    addLog("TCP 连接已断开");

    m_heartbeatTimer->stop();

    m_recvBuffer.clear();
    m_currentDeviceId = 0;
    m_deviceMap.clear();
    m_deviceDataMap.clear();
    m_deviceList->clear();

    m_connectBtn->setEnabled(true);
    m_disconnectBtn->setEnabled(false);
    m_startDeviceBtn->setEnabled(false);
    m_stopDeviceBtn->setEnabled(false);
    updateControlPanel(false);
    m_deviceInfoLabel->setText("请选择设备");
}

void Index::onReadyRead()
{
    m_recvBuffer.append(m_socket->readAll());

    while (true) {
        quint16 deviceId = 0;
        quint8 type = 0;
        QByteArray payload;

        if (m_protocol.parsePacket(m_recvBuffer, deviceId, payload, type)) {
            processPacket(deviceId, payload, type);
            int totalLen = sizeof(ProtocolHeader) + payload.size();
            m_recvBuffer.remove(0, totalLen);
        } else {
            break;
        }
    }
}

void Index::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    addLog(QString("Socket错误: %1").arg(m_socket->errorString()));
}

// ============================================================================
// 协议相关方法
// ============================================================================

void Index::sendRegisterPacket()
{
    QByteArray packet = m_protocol.buildPacket(MSG_TYPE_REGISTER, 0);
    m_socket->write(packet);
}

void Index::processPacket(quint16 deviceId, const QByteArray &payload, quint8 type)
{
    switch (type) {
    case MSG_TYPE_DEVICE_LIST: {
        if (payload.size() >= sizeof(DeviceInfoPayload)) {
            DeviceInfoPayload info;
            memcpy(&info, payload.data(), sizeof(info));

            QString name = QString::fromUtf8(info.name, strnlen(info.name, 63));
            QString group = QString::fromUtf8(info.group_name, strnlen(info.group_name, 63));
            bool online = (info.online == 1);

            updateDeviceList(deviceId, name, group, online);

            if (online) {
                addLog(QString("设备上线: [%1] %2").arg(deviceId).arg(name));
            }
        }
        break;
    }

    case MSG_TYPE_DATA_REPORT: {
        processDataReport(deviceId, payload);
        break;
    }

    case MSG_TYPE_HEARTBEAT: {
        break;
    }

    default:
        addLog(QString("收到未知包 type=0x%1").arg(type, 2, 16, QChar('0')));
        break;
    }
}

// ============================================================================
// 数据解析辅助方法
// ============================================================================

void Index::processDataReport(quint16 deviceId, const QByteArray &payload)
{
    if (payload.size() < sizeof(DataPayload)) {
        addLog(QString("数据上报负载长度异常: %1").arg(payload.size()));
        return;
    }

    DataPayload data;
    memcpy(&data, payload.data(), sizeof(DataPayload));

    qint16 temperature = qFromBigEndian(data.temperature);
    quint8 status = data.status;
    quint8 humi = data.humi;
    qint32 production = qFromBigEndian(data.production);

    float tempC = temperature / 100.0f;

    updateDeviceData(deviceId, tempC, humi, status, (quint32)production);

    if (m_currentDeviceId == deviceId) {
        updateDeviceInfoDisplay(deviceId);
    }
}

void Index::updateDeviceData(quint16 deviceId, float temperature, quint8 humi,
                              quint8 status, quint32 production)
{
    DeviceData &data = m_deviceDataMap[deviceId];
    data.temperature = temperature;
    data.humi = humi;
    data.status = status;
    data.production = production;
    data.timestamp = time(nullptr);
    data.hasData = true;

    if (m_currentDeviceId == deviceId) {
        updateDeviceInfoDisplay(deviceId);
    }
}

void Index::updateDeviceInfoDisplay(quint16 deviceId)
{
    if (!m_deviceDataMap.contains(deviceId)) {
        m_deviceInfoLabel->setText(QString("设备: %1 (无数据)").arg(deviceId));
        return;
    }

    DeviceData &data = m_deviceDataMap[deviceId];
    QString statusText = data.status ? "运行" : "停机";
    QString irText = data.production ? "检测到" : "无";

    QString info = QString("设备: %1 | %2℃ %3%% | %4 | 红外%5")
        .arg(deviceId)
        .arg(data.temperature, 0, 'f', 1)
        .arg(data.humi)
        .arg(statusText)
        .arg(irText);
    m_deviceInfoLabel->setText(info);
}

QListWidgetItem* Index::findDeviceItem(quint16 deviceId)
{
    for (int i = 0; i < m_deviceList->count(); ++i) {
        QListWidgetItem *item = m_deviceList->item(i);
        if (item->data(Qt::UserRole).toUInt() == deviceId) {
            return item;
        }
    }
    return nullptr;
}

void Index::onDeviceSelected(int row)
{
    QListWidgetItem *item = m_deviceList->item(row);
    if (!item) return;

    quint16 deviceId = item->data(Qt::UserRole).toUInt();
    if (deviceId != m_currentDeviceId) {
        m_currentDeviceId = deviceId;
        updateDeviceInfoDisplay(deviceId);

        QString name = m_deviceMap.value(deviceId, QString("设备%1").arg(deviceId));
        addLog(QString("选中设备 [%1] %2").arg(deviceId).arg(name));
        updateControlPanel(true);
    }
}

// ============================================================================
// 进程事件处理
// ============================================================================

void Index::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    // 停止所有定时器
    m_processCheckTimer->stop();
    m_startTimeoutTimer->stop();

    if (m_progressDialog) {
        m_progressDialog->close();
        delete m_progressDialog;
        m_progressDialog = nullptr;
    }

    // 防重复：若 m_process 已被清理则直接返回
    if (!m_process) return;

    if (exitCode == 0) {
        addLog("collector 进程正常退出（GPIO 资源已释放）");
    } else {
        addLog(QString("collector 进程退出，退出码: %1").arg(exitCode));
        addLog("[提示] 若退出码非0，请查看上方 [collector] 日志输出");
    }

    m_isDeviceConnected = false;
    m_isStartingDevice = false;
    m_isStoppingDevice = false;
    updateDeviceStatus(false);
    m_stopDeviceBtn->setEnabled(false);
    m_startDeviceBtn->setEnabled(true);
    m_startDeviceBtn->setText("启动");
    updateControlPanel(false);
    m_process->deleteLater();
    m_process = nullptr;
}

void Index::onProcessError(QProcess::ProcessError error)
{
    // 停止所有定时器
    m_processCheckTimer->stop();
    m_startTimeoutTimer->stop();

    if (m_progressDialog) {
        m_progressDialog->close();
        delete m_progressDialog;
        m_progressDialog = nullptr;
    }

    // 防重复：若 m_process 已被清理则直接返回
    if (!m_process) return;

    QString errorMsg;
    switch(error) {
        case QProcess::FailedToStart:
            errorMsg = "进程启动失败（程序不存在/无执行权限/工作目录不存在）";
            break;
        case QProcess::Crashed:
            errorMsg = "进程崩溃";
            break;
        case QProcess::Timedout:
            errorMsg = "超时";
            break;
        case QProcess::WriteError:
            errorMsg = "写入错误";
            break;
        case QProcess::ReadError:
            errorMsg = "读取错误";
            break;
        default:
            errorMsg = "未知错误";
            break;
    }
    addLog("[错误] " + errorMsg);

    m_isDeviceConnected = false;
    m_isStartingDevice = false;
    m_isStoppingDevice = false;
    updateDeviceStatus(false);
    m_stopDeviceBtn->setEnabled(false);
    m_startDeviceBtn->setEnabled(true);
    m_startDeviceBtn->setText("启动");
    updateControlPanel(false);
    m_process->deleteLater();
    m_process = nullptr;
}

void Index::onProcessReadyRead()
{
    if (!m_process) return;

    QString output = m_process->readAllStandardOutput();
    QString error = m_process->readAllStandardError();

    if (!output.isEmpty()) {
        QStringList lines = output.split('\n', QString::SkipEmptyParts);
        for (const QString &line : lines) {
            addLog("[collector] " + line.trimmed());
        }
    }
    if (!error.isEmpty()) {
        QStringList lines = error.split('\n', QString::SkipEmptyParts);
        for (const QString &line : lines) {
            addLog("[collector错误] " + line.trimmed());
        }
    }
}

// ============================================================================
// UI 更新方法
// ============================================================================

void Index::updateConnectionStatus(bool connected)
{
    m_statusIndicator->setStyleSheet(connected ?
        "border-radius: 6px; background: #a6e3a1;" :
        "border-radius: 6px; background: #f38ba8;");
    m_statusLabel->setText(connected ? "已连接" : "未连接");
}

void Index::updateDeviceStatus(bool connected)
{
    if (connected) {
        m_deviceStatusLabel->setText("设备在线");
        m_deviceStatusLabel->setStyleSheet("color: #a6e3a1;");
        m_stopDeviceBtn->setEnabled(true);
    } else {
        m_deviceStatusLabel->setText("设备离线");
        m_deviceStatusLabel->setStyleSheet("color: #f38ba8;");
        m_stopDeviceBtn->setEnabled(false);
    }
}

void Index::updateControlPanel(bool enabled)
{
    bool canStart = m_isConnected && !m_isStartingDevice;
    bool canStop = m_isDeviceConnected && enabled;

    m_startDeviceBtn->setEnabled(canStart);
    m_stopDeviceBtn->setEnabled(canStop);
}

void Index::updateDeviceList(quint16 deviceId, const QString &name,
                              const QString &group, bool online)
{
    QString displayName = QString("[%1] %2").arg(deviceId).arg(name);
    if (!group.isEmpty()) {
        displayName += QString(" (%3)").arg(group);
    }

    QListWidgetItem *item = nullptr;
    for (int i = 0; i < m_deviceList->count(); ++i) {
        QListWidgetItem *it = m_deviceList->item(i);
        if (it->data(Qt::UserRole).toUInt() == deviceId) {
            item = it;
            break;
        }
    }

    if (!item) {
        item = new QListWidgetItem(displayName, m_deviceList);
        item->setData(Qt::UserRole, deviceId);
        m_deviceMap[deviceId] = name;
    } else {
        item->setText(displayName);
    }

    item->setForeground(online ? Qt::green : Qt::gray);

    if (online && m_currentDeviceId == 0) {
        m_deviceList->setCurrentRow(m_deviceList->row(item));
        onDeviceSelected(m_deviceList->row(item));
    }
}

void Index::addLog(const QString &msg)
{
    QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logTextEdit->append(QString("[%1] %2").arg(time).arg(msg));
    m_logTextEdit->moveCursor(QTextCursor::End);
}
