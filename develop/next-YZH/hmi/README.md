# 车间设备集中监控系统 HMI（成员 C · Qt 大屏）

针对 **野火 i.MX6ULL MINI + 4.3 寸 480×272 触摸屏** 的工业级监控看板第一版。
开发环境：Qt 5.9.8 (MinGW 32bit) / Windows；C++11；**不依赖 QtCharts**（曲线为纯 QPainter 自绘，交叉编译零额外依赖）。

## 一、功能与页面

底部导航切换三个页面：

| 页面 | 内容 |
|------|------|
| **实时看板** | 顶部 4 张总览卡片（在线设备 / 平均温度 / 累计产量 / 活动告警）；左侧设备状态列表（LED 状态灯 + 实时温度，点击行切换曲线设备，告警设备 LED 闪烁）；中部 120 秒滑动窗口温度趋势曲线（渐变填充 + 末端数值气泡）；右侧实时告警滚动列表（告警红 / 预警橙 / 恢复绿） |
| **历史回查** | 选设备 + 最近 N 分钟（5~60）→ 曲线回放 + 记录表格，切到本页自动查询 |
| **告警记录** | 全部告警表格：触发时间 / 设备 / 级别 / 描述 / 状态（告警中 / 已恢复），实时刷新 |

内置告警引擎（`DataManager`）：温度 ≥75°C 告警、≥60°C 预警、回落 ≤58°C 自动恢复（滞回防抖），级别只升不降。

当前数据来自 **模拟器**（`DataSimulator`，1Hz、6 台设备，含随机游走 / 高温尖峰 / 偶发掉线），顶栏显示「数据源: 模拟」。

## 二、Windows 上运行

```bash
# Qt Creator: 直接打开 hmi.pro，选 Qt 5.9.8 MinGW 套件，运行即可
# 或命令行:
qmake hmi.pro
mingw32-make release
# 运行时需保证 Qt\bin 在 PATH 中
WorkshopHMI.exe              # 480x272 固定窗口预览
WorkshopHMI.exe -fullscreen  # 全屏（板上用）
```

## 三、交叉编译到 i.MX6ULL（要点）

1. 用 ARM 工具链 qmake/ make 交叉编译（野火 BSP 配套的 qt5 环境），源码只用 Qt Widgets + C++11，无 web/charts 依赖。
2. 板上运行：
   ```bash
   export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=480x272   # 或 eglfs
   export QT_QPA_GENERIC_PLUGINS=tslib        # 4.3 寸电阻触摸
   export TSLIB_TSDEVICE=/dev/input/event1    # 按实际触摸设备
   ./WorkshopHMI -fullscreen
   ```
3. 中文字体：把 `simsun.ttc`/`simhei.ttf` 拷到 `/usr/share/fonts/` 并 `fc-cache`（代码使用像素字号，缺字体时自动回退，不会乱布局）。

## 四、如何接入真实数据（替换模拟器）

整条链路已按文档 §一 的分层解耦，接真数据只改 `MainWindow` 里一处连接：

```
[成员A服务端] Epoll 收包 → 协议解析(魔数/长度/CRC)
    → 组装 DeviceData{ id, temp, runStatus, output, online, ts }
    → 跨线程信号槽: emit deviceData(data)   ← 1Hz 每设备一条
    → DataManager::onDeviceData()           (历史缓冲/告警判定都在这里)
    → deviceUpdated()/alarmRaised() 信号 → 界面刷新
```

- 删掉 `m_sim` 相关两行，把服务端解析线程的信号连到 `DataManager::instance()` 即可；
- 「历史回查」页当前查内存环形缓冲（每设备 1 小时），成员 D 的 SQLite 接口完成后，
  把 `HistoryPage::onQuery()` 里的 `recentHistory()` 换成数据库查询即可，界面不动；
- 顶栏「数据源: 模拟」换成 TCP 连接状态 LED（成员 A 提供连接/断开信号）。

## 五、目录结构

```
hmi/
├── hmi.pro
└── src/
    ├── main.cpp               # 入口 + 全局工业深色 QSS
    ├── datatypes.h            # Sample / DeviceData / AlarmItem
    ├── theme.h                # 统一配色
    ├── mainwindow.*           # 顶栏 + 页面堆栈 + 底部导航
    ├── core/
    │   ├── datamanager.*      # 数据中枢: 实时值/历史环形缓冲/告警引擎
    │   └── datasimulator.*    # 模拟数据源(接真数据时替换)
    ├── widgets/
    │   ├── trendchart.*       # 纯 QPainter 趋势曲线(滑动窗口/自适应范围)
    │   ├── overviewcard.*     # 总览指标卡片(告警红框)
    │   ├── devicelist.*       # 设备状态列表(LED/闪烁/点选)
    │   └── alarmpanel.*       # 实时告警滚动列表
    └── pages/
        ├── dashboardpage.*    # 实时看板
        ├── historypage.*      # 历史回查
        └── recordspage.*      # 告警记录
```
