#pragma once
#include <QColor>

// 工业深色主题统一配色
namespace Theme {
const QColor Bg        = QColor(0x0e, 0x14, 0x1a);  // 全局背景
const QColor Panel     = QColor(0x16, 0x20, 0x2a);  // 面板背景
const QColor PanelLine = QColor(0x2b, 0x3a, 0x48);  // 面板描边
const QColor Grid      = QColor(0x24, 0x30, 0x3c);  // 图表网格
const QColor Text      = QColor(0xd8, 0xe2, 0xea);  // 主文字
const QColor Dim       = QColor(0x7c, 0x8d, 0x9c);  // 次要文字
const QColor Accent    = QColor(0x00, 0xa8, 0xff);  // 主题强调色
const QColor Ok        = QColor(0x2e, 0xcc, 0x71);  // 正常/运行
const QColor Warn      = QColor(0xf3, 0x9c, 0x12);  // 预警
const QColor Danger    = QColor(0xe7, 0x4c, 0x3c);  // 告警
const QColor Offline   = QColor(0x55, 0x60, 0x6b);  // 离线
}
