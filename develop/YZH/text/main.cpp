/**
 * 车间设备数据采集与可视化监控系统 - Qt 大屏看板入口
 *
 * 说明：
 *  - 当前版本为"模拟数据版"（实施文档阶段2：Qt 界面接收模拟数据）；
 *  - 界面线程只通过定时器/信号槽刷界面，不做任何阻塞读锁（文档红线要求）；
 *  - Qt 5.9.8 + MinGW 32bit 编译通过；后续交叉编译到 i.MX6ULL 时无需改动 UI 代码。
 */
#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Windows 下优先用微软雅黑；板子上若无此字体 Qt 会自动回退
    QFont f(QString::fromUtf8("Microsoft YaHei"), 9);
    a.setFont(f);

    MainWindow w;
    w.show();

    return a.exec();
}
