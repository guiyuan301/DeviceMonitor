#include <QApplication>
#include <QFont>
#include "mainwindow.h"
#include "datatypes.h"
// 新版本
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 480x272 小屏: 统一用像素字号, 避免不同平台 DPI 缩放导致布局错乱
    QFont f("Microsoft YaHei");
    f.setPixelSize(10);
    a.setFont(f);

    qRegisterMetaType<DeviceData>("DeviceData");
    qRegisterMetaType<AlarmItem>("AlarmItem");

    // 全局工业深色风格
    a.setStyleSheet(R"(
* { outline: none; }
QWidget { background-color:#0e141a; color:#d8e2ea; }
QLabel { background:transparent; }

QFrame#topBar  { background:#131c25; border-bottom:1px solid #2b3a48; }
QFrame#navBar  { background:#131c25; border-top:1px solid #2b3a48; }
QPushButton#navBtn { background:transparent; color:#7c8d9c; border:none;
    border-top:2px solid transparent; padding:2px 14px; font-size:11px; }
QPushButton#navBtn:hover { color:#aebdca; }
QPushButton#navBtn:checked { color:#00a8ff; border-top:2px solid #00a8ff; background:#16202a; }

QFrame#panel { background:#16202a; border:1px solid #2b3a48; border-radius:2px; }
QLabel#panelTitle { color:#7c8d9c; font-size:9px; }
QLabel#clockLabel { color:#d8e2ea; font-size:11px; }
QLabel#netLabel   { color:#7c8d9c; font-size:9px; }

QFrame#overviewCard { background:#16202a; border:1px solid #2b3a48; border-radius:2px; }
QFrame#overviewCard[alarm="true"] { border:1px solid #e74c3c; background:#241518; }
QLabel#cardTitle { color:#7c8d9c; font-size:9px; }
QLabel#cardValue { color:#eaf2f8; font-size:17px; font-weight:bold; }
QLabel#cardUnit  { color:#7c8d9c; font-size:9px; }
QLabel#cardSub   { color:#7c8d9c; font-size:9px; }

QComboBox, QSpinBox { background:#1b2733; border:1px solid #2b3a48; border-radius:2px;
    padding:1px 4px; font-size:10px; color:#d8e2ea; }
QComboBox::drop-down { border:none; width:14px; }
QComboBox QAbstractItemView { background:#1b2733; border:1px solid #2b3a48;
    selection-background-color:#005f87; selection-color:#ffffff; font-size:10px; }
QPushButton#queryBtn { background:#0a6d99; border:1px solid #0a86bd; border-radius:2px;
    padding:2px 10px; font-size:10px; color:#eaf6ff; }
QPushButton#queryBtn:hover  { background:#0a86bd; }
QPushButton#queryBtn:pressed { background:#085a80; }
QPushButton#chartBtn { background:#1b2733; color:#7c8d9c; border:1px solid #2b3a48;
    border-radius:2px; padding:1px 8px; font-size:9px; }
QPushButton#chartBtn:hover { color:#aebdca; }
QPushButton#chartBtn:checked { color:#00a8ff; border:1px solid #00a8ff; }
QPushButton#chartBtn:disabled { color:#4a5560; border-color:#24313d; }
QLabel#snapThumb { background:#10161d; border:1px solid #2b3a48; }

QTableWidget { background:#121920; alternate-background-color:#16202a; gridline-color:#22303c;
    border:1px solid #2b3a48; font-size:9px;
    selection-background-color:#0a5580; selection-color:#eaf2f8; }
QHeaderView::section { background:#1b2733; color:#7c8d9c; border:none;
    border-right:1px solid #22303c; border-bottom:1px solid #2b3a48;
    padding:2px 4px; font-size:9px; }

QScrollBar:vertical { background:#121920; width:6px; margin:0; }
QScrollBar::handle:vertical { background:#2b3a48; min-height:20px; border-radius:3px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar:horizontal { background:#121920; height:6px; margin:0; }
QScrollBar::handle:horizontal { background:#2b3a48; min-width:20px; border-radius:3px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }

QScrollArea { border:none; background:transparent; }
)");

    MainWindow w;
    // 板上全屏运行: WorkshopHMI -fullscreen
    if (a.arguments().contains("-fullscreen"))
        w.showFullScreen();
    else
        w.show();
    return a.exec();
}
