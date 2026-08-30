#-------------------------------------------------
# 车间设备集中监控系统 HMI (Qt 5.9+, 480x272 工业看板)
# 目标平台: Windows 预览 / 野火 i.MX6ULL MINI + 4.3寸屏
#-------------------------------------------------
QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG   += c++11
TARGET    = WorkshopHMI
TEMPLATE  = app

# MSVC 编译时保证源码(UTF-8)中的中文不乱码
msvc {
    QMAKE_CXXFLAGS += /utf-8
}

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/core/datamanager.cpp \
    src/core/datasimulator.cpp \
    src/widgets/trendchart.cpp \
    src/widgets/overviewcard.cpp \
    src/widgets/devicelist.cpp \
    src/widgets/alarmpanel.cpp \
    src/widgets/videowidget.cpp \
    src/pages/dashboardpage.cpp \
    src/pages/historypage.cpp \
    src/pages/videopage.cpp \
    src/pages/recordspage.cpp

HEADERS += \
    src/mainwindow.h \
    src/datatypes.h \
    src/theme.h \
    src/core/datamanager.h \
    src/core/datasimulator.h \
    src/widgets/trendchart.h \
    src/widgets/overviewcard.h \
    src/widgets/devicelist.h \
    src/widgets/alarmpanel.h \
    src/widgets/videowidget.h \
    src/pages/dashboardpage.h \
    src/pages/historypage.h \
    src/pages/videopage.h \
    src/pages/recordspage.h
