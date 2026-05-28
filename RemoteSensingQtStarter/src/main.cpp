/**
 * @file main.cpp
 * @brief 程序入口点，启动 Qt 应用程序并显示主窗口
 *
 * 工作流程：
 *   1. 创建 QApplication 对象（管理 GUI 事件循环）
 *   2. 创建主窗口 rs::MainWindow，设置初始大小
 *   3. 调用 show() 显示窗口
 *   4. 进入事件循环 app.exec()，等待用户操作
 */

#include "rs/MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    // 创建 Qt 应用程序实例，管理全局资源（字体、样式、命令行参数等）
    QApplication app(argc, argv);

    // 创建主窗口，含菜单栏、图层树、影像显示区、日志面板等
    rs::MainWindow window;
    window.resize(1200, 760);  // 设置窗口初始尺寸（宽 1200px × 高 760px）
    window.show();             // 显示窗口（在此之前窗口不可见）

    // 进入 Qt 事件主循环，等待并分发用户交互（鼠标、键盘、定时器等）
    // 当最后一个窗口关闭时，app.exec() 返回，程序结束
    return app.exec();
}

