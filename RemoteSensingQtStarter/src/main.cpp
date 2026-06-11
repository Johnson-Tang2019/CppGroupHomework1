#include "rs/MainWindow.h"
#include <QApplication>
#include <QDebug>
#include <gdal_priv.h>
//2026.6.6

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setStyle("Fusion");
    QString darkTheme = R"(
        /* ── 全局默认 ── */
        QWidget { background-color: #1e1e1e; color: #d4d4d4; }
        QMainWindow { background-color: #1e1e1e; }

        /* ── 菜单栏 ── */
        QMenuBar { background-color: #2d2d30; color: #ffffff; }
        QMenuBar::item:selected { background-color: #3e3e42; }
        QMenu { background-color: #2d2d30; color: #ffffff; border: 1px solid #454545; }
        QMenu::item:selected { background-color: #007acc; }
        QMenu::separator { height: 1px; background: #454545; margin: 4px 8px; }

        /* ── 图层树 ── */
        QTreeWidget { background-color: #252526; color: #d4d4d4; border: none; outline: none; }
        QTreeWidget::item:selected { background-color: #007acc; color: #ffffff; }
        QTreeWidget::item:hover { background-color: #2a2d2e; }
        QHeaderView::section { background-color: #2d2d30; color: #ccc; border: none; padding: 4px 8px; }

        /* ── 日志面板 ── */
        QTextEdit { background-color: #1e1e1e; color: #4ec9b0; font-family: Consolas; border-top: 2px solid #007acc; }

        /* ── 标签页 ── */
        QTabWidget::pane { background-color: #1e1e1e; border: 1px solid #3e3e42; }
        QTabBar::tab { background: #2d2d30; color: #999; padding: 8px 20px; border: 1px solid #1e1e1e; }
        QTabBar::tab:selected { background: #1e1e1e; color: #fff; border-top: 2px solid #007acc; }
        QTabBar::tab:hover:!selected { background: #3e3e42; }

        /* ── 图形视图（影像显示区） ── */
        QGraphicsView { background-color: #0d0d0d; border: none; }

        /* ── 分割器 ── */
        QSplitter::handle { background-color: #3e3e42; margin: 0 3px; }
        QSplitter::handle:hover { background-color: #007acc; }

        /* ── 状态栏 ── */
        QStatusBar { background-color: #007acc; color: white; font-weight: bold; }

        /* ── 对话框 ── */
        QDialog { background-color: #2d2d30; }
        QLabel { color: #d4d4d4; background: transparent; }
        QPushButton {
            background-color: #3e3e42; color: #d4d4d4;
            border: 1px solid #555; border-radius: 4px;
            padding: 6px 16px; min-width: 80px;
        }
        QPushButton:hover { background-color: #505050; border-color: #007acc; }
        QPushButton:pressed { background-color: #007acc; }
        QPushButton:default { background-color: #007acc; border-color: #007acc; }
        QPushButton:disabled { background-color: #2a2a2a; color: #666; border-color: #3a3a3a; }

        /* ── 输入框 ── */
        QLineEdit {
            background-color: #3c3c3c; color: #d4d4d4;
            border: 1px solid #555; border-radius: 3px; padding: 4px 8px;
        }
        QLineEdit:focus { border-color: #007acc; }

        /* ── 组合框 / 下拉列表 ── */
        QComboBox {
            background-color: #3c3c3c; color: #d4d4d4;
            border: 1px solid #555; border-radius: 3px; padding: 4px 8px;
        }
        QComboBox:hover { border-color: #007acc; }
        QComboBox QAbstractItemView {
            background-color: #2d2d30; color: #d4d4d4;
            selection-background-color: #007acc; selection-color: #ffffff;
            border: 1px solid #555;
        }

        /* ── 微调框 ── */
        QSpinBox, QDoubleSpinBox {
            background-color: #3c3c3c; color: #d4d4d4;
            border: 1px solid #555; border-radius: 3px; padding: 4px 6px;
        }
        QSpinBox:focus, QDoubleSpinBox:focus { border-color: #007acc; }

        /* ── 滚动条 ── */
        QScrollBar:vertical {
            background: #1e1e1e; width: 12px; border: none;
        }
        QScrollBar::handle:vertical {
            background: #424242; min-height: 30px; border-radius: 6px; margin: 2px;
        }
        QScrollBar::handle:vertical:hover { background: #686868; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal {
            background: #1e1e1e; height: 12px; border: none;
        }
        QScrollBar::handle:horizontal {
            background: #424242; min-width: 30px; border-radius: 6px; margin: 2px;
        }
        QScrollBar::handle:horizontal:hover { background: #686868; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

        /* ── 提示框 ── */
        QToolTip { background-color: #1e1e1e; color: #d4d4d4; border: 1px solid #007acc; padding: 4px; }
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