#-------------------------------------------------
# 车间设备监控系统 - Qt 大屏看板 (hmi)
# Qt 5.9.8 / MinGW 32bit 兼容，C++11
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET   = WorkshopHMI
TEMPLATE = app
CONFIG   += c++11

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    devicecard.cpp \
    trendchart.cpp \
    db/sqlite3.c \
    db/storage.c \
    db/alarm.c \

HEADERS += \
    mainwindow.h \
    devicecard.h \
    trendchart.h \
    db/sqlite3.h \
    db/storage.h \
    db/alarm.h
