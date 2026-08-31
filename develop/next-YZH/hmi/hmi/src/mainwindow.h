#pragma once
#include <QWidget>

class QLabel;
class QStackedWidget;
class DataSimulator;

/*
 * 主窗口: 顶栏(标题/数据源状态/时钟) + 页面堆栈 + 底部导航。
 * 固定 480x272 (与 4.3 寸屏一致); 板上加 -fullscreen 参数全屏运行。
 */
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onClock();

private:
    QLabel *m_netLed;
    QLabel *m_netLabel;
    QLabel *m_clock;
    QStackedWidget *m_stack;
    DataSimulator *m_sim;
};
