#include "rs/MainWindow.h"
#include <QApplication>
#include <QDebug>
//2026.6.6
#ifdef RS_WITH_GDAL
#include <gdal_priv.h>
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 【UI 升级】启用现代极客风暗色主题 (Dark Theme)
    app.setStyle("Fusion");
    QString darkTheme = R"(
        QMainWindow { background-color: #1e1e1e; color: #d4d4d4; }
        QMenuBar { background-color: #2d2d30; color: #ffffff; }
        QMenuBar::item:selected { background-color: #3e3e42; }
        QMenu { background-color: #2d2d30; color: #ffffff; border: 1px solid #454545; }
        QMenu::item:selected { background-color: #007acc; }
        QTreeWidget { background-color: #252526; color: #d4d4d4; border: none; }
        QTreeWidget::item:selected { background-color: #007acc; color: #ffffff; }
        QTextEdit { background-color: #1e1e1e; color: #4ec9b0; font-family: Consolas; border-top: 2px solid #007acc; }
        QTabBar::tab { background: #2d2d30; color: #999; padding: 8px 20px; border: 1px solid #1e1e1e; }
        QTabBar::tab:selected { background: #1e1e1e; color: #fff; border-top: 2px solid #007acc; }
        QStatusBar { background-color: #007acc; color: white; font-weight: bold; }
    )";
    app.setStyleSheet(darkTheme);

#ifdef RS_WITH_GDAL
    try {
        GDALAllRegister();
        qDebug() << "GDAL initialized.";
    } catch (...) {
        qDebug() << "GDAL initialization failed.";
    }
#endif

    rs::MainWindow window;
    window.resize(1280, 800); // 扩大默认窗口比例
    window.show();
    return app.exec();
}