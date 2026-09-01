#-------------------------------------------------
# 车间设备集中监控系统 HMI (Qt 5.9+, 480x272 工业看板)
# 目标平台: Windows 预览 / 野火 i.MX6ULL MINI + 4.3寸屏
#-------------------------------------------------
QT       += core gui network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG   += c++11
TARGET    = WorkshopHMI
TEMPLATE  = app

# MSVC 编译时保证源码(UTF-8)中的中文不乱码
msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

# 成员D数据库模块(XS_/db)为纯C代码, 需要 C99(for内声明变量)
CONFIG += c99

INCLUDEPATH += XS_/db

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/core/datamanager.cpp \
    src/core/datasimulator.cpp \
    src/core/serverclient.cpp \
    src/core/dbbridge.cpp \
    src/widgets/trendchart.cpp \
    src/widgets/overviewcard.cpp \
    src/widgets/devicelist.cpp \
    src/widgets/alarmpanel.cpp \
    src/widgets/videowidget.cpp \
    src/pages/dashboardpage.cpp \
    src/pages/historypage.cpp \
    src/pages/videopage.cpp \
    src/pages/recordspage.cpp \
    XS_/db/sqlite3.c \
    XS_/db/storage.c \
    XS_/db/alarm.c

HEADERS += \
    src/mainwindow.h \
    src/datatypes.h \
    src/theme.h \
    src/core/datamanager.h \
    src/core/datasimulator.h \
    src/core/serverclient.h \
    src/core/dbbridge.h \
    src/widgets/trendchart.h \
    src/widgets/overviewcard.h \
    src/widgets/devicelist.h \
    src/widgets/alarmpanel.h \
    src/widgets/videowidget.h \
    src/pages/dashboardpage.h \
    src/pages/historypage.h \
    src/pages/videopage.h \
    src/pages/recordspage.h \
    XS_/db/sqlite3.h \
    XS_/db/storage.h \
    XS_/db/alarm.h

# sqlite3 amalgamation 体量大, 关闭告警避免刷屏
sqlite3.c.CFLAGS = -w

# ---------------------------------------------------------------
# Linux 交叉编译(野火 i.MX6ULL / LubanCat)修复:
# sqlite3.c 用到 dlopen, 新版 gcc/binutils 默认 --as-needed,
# 不会自动链入 libdl, 必须显式 -ldl; pthread/math 一并补上
unix:!macx {
    LIBS += -ldl -lpthread -lm
}

# ---------------------------------------------------------------
# 板上精简开关(内存紧张时打开): 去掉抓拍/视频的内存占用
# 用法: qmake 时 "DEFINES += HMI_LITE" 或在下面取消注释
#DEFINES += HMI_LITE
contains(DEFINES, HMI_LITE) {
    message("HMI_LITE: 禁用抓拍功能, 减小内存占用")
    DEFINES += HMI_DISABLE_SNAPSHOT
}
