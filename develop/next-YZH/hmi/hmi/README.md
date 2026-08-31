# 车间环境与产线监控系统 HMI（成员 C · Qt 大屏）V3

针对 **野火 i.MX6ULL MINI + 4.3 寸 480×272 触摸屏** 的工业级监控看板。
开发环境：Qt 5.9.8 (MinGW 32bit) / Windows；C++11；**不依赖 QtCharts / QtMultimedia**
（曲线 QPainter 自绘；视频监控用 QProcess + ffmpeg 拉流转帧）。

> V3：**产量=红外对管计数**（每 10 件自动抽检抓拍）+ **告警留档/手动抓拍** + **FFmpeg 实时视频监控页**。
> 对接服务端/数据库/采集板完整接口见 `../docs/项目文档-V3.md`。

## 一、功能与页面（V3）

底部导航切换四个页面：

| 页面 | 内容 |
|------|------|
| **实时看板** | 5 张总览卡片（在线设备/平均温度/平均湿度/**今日产量**/活动告警）；采集点列表（LED+温湿度，点击切换曲线）；温/湿度趋势曲线切换（120s 滑动窗）；实时告警（仅告警中）+ **消音按钮** |
| **历史回查** | 选采集点 + 5~60 分钟 → 温湿度双曲线 + 记录表格（时间/温度/湿度/**产量**），切页自动查询 |
| **视频监控** | 实时画面：**FFmpeg 拉流**（QProcess 解析 MJPEG，命令可界面配置并持久化）/ 模拟产线画面（无摄像头时演示）；**手动抓拍**（存入 snaps，告警记录页可回看）；状态栏显示分辨率+fps |
| **告警记录** | 告警+抽检事件表格（类型/级别/状态着色，`[抓拍]`标记）；右侧**抓拍预览** |

产量闭环：红外对管每过一件计数 +1 → 大屏产量卡片/历史表格累加；**每 10 件（kSnapEveryN 可配）自动"抽检"抓拍**；级别 2 告警自动"告警留档"抓拍；视频页可"手动"抓拍——三种抓拍都进入 snaps 目录并在告警记录页可回看。

告警引擎（`DataManager`）：温度 ≥45°C 告警 / ≥35°C 预警；湿度 ≥85% 告警 / ≥70% 或 ≤30% 预警；离线=心跳超时。蜂鸣器只在级别 2 告警鸣响，消音 300s。

当前数据来自 **模拟器**（3 个采集点：车间A 高温尖峰 / 车间B 产线出件 / 仓库C 高湿尖峰），抓拍图为合成占位图，存 `build/release/snaps/`。

## 二、Windows 上运行

```bash
# Qt Creator: 直接打开 hmi.pro，选 Qt 5.9.8 MinGW 套件，运行即可
# 或命令行:
qmake hmi.pro && mingw32-make release
WorkshopHMI.exe              # 480x272 固定窗口预览
WorkshopHMI.exe -fullscreen  # 全屏（板上用）
```

## 三、交叉编译到 i.MX6ULL（要点）

```bash
# 板上运行:
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=480x272   # 或 eglfs
export QT_QPA_GENERIC_PLUGINS=tslib        # 4.3 寸电阻触摸
./WorkshopHMI -fullscreen
# 中文字体: 拷 simhei.ttf 到 /usr/share/fonts 并 fc-cache
```

## 四、数据落库与接入真实数据

**V4 起数据库已合并**（成员 D 的 `XS_/db` 模块，SQLite 3.52）：启动自动打开
`monitor.db`（WAL），采样/告警/抓拍自动落库，历史回查走数据库，重启自动回灌记录。
桥接层 `src/core/dbbridge.cpp`；表结构差异与迁移说明见 `../docs/数据库合并文档-V4.md`。

接真实服务端时按 **项目文档-V3 §五** 调用 `DataManager` 四个接口（模拟器删除即可），
落库链路不变；服务端正式接入后写库应移交服务端入库线程（merge 文档遗留项 #9）。

## 五、目录结构

```
hmi/
├── hmi.pro
└── src/
    ├── main.cpp               # 入口 + 全局工业深色 QSS
    ├── datatypes.h            # Sample / DeviceData / AlarmItem / SnapItem
    ├── theme.h                # 统一配色
    ├── mainwindow.*           # 顶栏 + 页面堆栈 + 底部导航(4页)
    ├── core/
    │   ├── datamanager.*      # 数据中枢: 实时值/历史/告警引擎/产量抽检/抓拍索引/消音
    │   └── datasimulator.*    # 模拟数据源(接真数据时替换)
    ├── widgets/
    │   ├── trendchart.*       # 纯 QPainter 趋势曲线(多序列/滑动窗口/自适应)
    │   ├── overviewcard.*     # 总览指标卡片(告警红框)
    │   ├── devicelist.*       # 采集点状态列表(LED/闪烁/点选)
    │   ├── alarmpanel.*       # 实时告警滚动列表
    │   └── videowidget.*      # 视频控件(QProcess+ffmpeg拉流 / 模拟画面双源)
    └── pages/
        ├── dashboardpage.*    # 实时看板(5卡片)
        ├── historypage.*      # 历史回查(温湿度双曲线+产量)
        ├── videopage.*        # 视频监控(FFmpeg/模拟 + 手动抓拍)
        └── recordspage.*      # 告警记录 + 抓拍预览
```
