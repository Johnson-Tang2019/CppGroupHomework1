#include "rs/MainWindow.h"
#include "rs/SplashScreen.h"
#include "rs/Translation.h"

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QScreen>

#ifdef RS_WITH_GDAL
#include <gdal_priv.h>
#endif

namespace {

void centerOnScreen(QWidget *widget) {
    if (!widget) {
        return;
    }
    if (QScreen *screen = QApplication::primaryScreen()) {
        const QRect geo = screen->availableGeometry();
        widget->move(geo.center() - widget->rect().center());
    }
}

void fitMainWindowToScreen(QWidget *widget) {
    if (!widget) {
        return;
    }

    QScreen *screen = QApplication::primaryScreen();
    if (!screen) {
        widget->resize(1280, 800);
        return;
    }

    const QRect geo = screen->availableGeometry();
    constexpr int kMinW = 960;
    constexpr int kMinH = 640;
    constexpr int kMargin = 8;

    // 尽量铺满可用屏幕，只留很窄的边距，避免超出任务栏区域
    int width = qMax(kMinW, geo.width() - kMargin * 2);
    int height = qMax(kMinH, geo.height() - kMargin * 2);

    widget->setMinimumSize(kMinW, kMinH);
    widget->resize(width, height);
    widget->move(geo.x() + kMargin, geo.y() + kMargin);
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CppGroupHomework"));
    QCoreApplication::setApplicationName(QStringLiteral("RemoteSensingQtStarter"));
    app.setStyle(QStringLiteral("Fusion"));
    app.setWindowIcon(QIcon(QStringLiteral(":/splash_hero.png")));
    QString pinkTheme = R"(
        /* ── 全局默认 ── */
        QWidget { background-color: #fff0f5; color: #4a2030; }
        QMainWindow { background-color: #fff0f5; }

        /* ── 菜单栏 ── */
        QMenuBar { background-color: #ffb6c1; color: #4a2030; }
        QMenuBar::item:selected { background-color: #ff69b4; color: #fff; }
        QMenu { background-color: #ffe4e9; color: #4a2030; border: 1px solid #ffb6c1; }
        QMenu::item:selected { background-color: #ff69b4; color: #fff; }
        QMenu::separator { height: 1px; background: #ffb6c1; margin: 4px 8px; }

        /* ── 图层树 ── */
        QTreeWidget { background-color: #fff5f8; color: #4a2030; border: none; outline: none; }
        QTreeWidget::item:selected { background-color: #ff69b4; color: #ffffff; }
        QTreeWidget::item:hover { background-color: #ffe4ec; }
        QHeaderView::section { background-color: #ffb6c1; color: #4a2030; border: none; padding: 4px 8px; }

        /* ── 日志面板 ── */
        QTextEdit { background-color: #fff0f5; color: #c71585; font-family: Consolas; border-top: 2px solid #ff69b4; }

        /* ── 标签页 ── */
        QTabWidget::pane { background-color: #fff0f5; border: 1px solid #ffb6c1; }
        QTabBar::tab { background: #ffb6c1; color: #4a2030; padding: 8px 20px; border: 1px solid #ffb6c1; }
        QTabBar::tab:selected { background: #fff0f5; color: #c71585; border-top: 2px solid #ff69b4; }
        QTabBar::tab:hover:!selected { background: #ffc0cb; }

        /* ── 图形视图（影像显示区） ── */
        QGraphicsView { background-color: #ffe4e9; border: none; }

        /* ── 分割器 ── */
        QSplitter::handle { background-color: #ffb6c1; margin: 0 3px; }
        QSplitter::handle:hover { background-color: #ff69b4; }

        /* ── 状态栏 ── */
        QStatusBar {
            background-color: #fff5f8;
            color: #5a4a4a;
            border-top: 1px solid #f4b8c8;
            font-weight: normal;
        }
        QStatusBar QLabel {
            color: #5a4a4a;
            background: transparent;
        }

        /* ── 对话框 ── */
        QDialog { background-color: #fff0f5; }
        QLabel { color: #4a2030; background: transparent; }
        QPushButton {
            background-color: #ffb6c1; color: #4a2030;
            border: 1px solid #ff69b4; border-radius: 4px;
            padding: 6px 16px; min-width: 80px;
        }
        QPushButton:hover { background-color: #ff69b4; color: #fff; }
        QPushButton:pressed { background-color: #c71585; color: #fff; }
        QPushButton:default { background-color: #ff69b4; color: #fff; border-color: #c71585; }
        QPushButton:disabled { background-color: #ffe4e9; color: #d4a0b0; border-color: #ffc0cb; }

        /* ── 输入框 ── */
        QLineEdit {
            background-color: #fff5f8; color: #4a2030;
            border: 1px solid #ffb6c1; border-radius: 3px; padding: 4px 8px;
        }
        QLineEdit:focus { border-color: #ff69b4; }

        /* ── 组合框 / 下拉列表 ── */
        QComboBox {
            background-color: #fff5f8; color: #4a2030;
            border: 1px solid #ffb6c1; border-radius: 3px; padding: 4px 8px;
        }
        QComboBox:hover { border-color: #ff69b4; }
        QComboBox QAbstractItemView {
            background-color: #fff0f5; color: #4a2030;
            selection-background-color: #ff69b4; selection-color: #ffffff;
            border: 1px solid #ffb6c1;
        }

        /* ── 微调框 ── */
        QSpinBox, QDoubleSpinBox {
            background-color: #fff5f8; color: #4a2030;
            border: 1px solid #ffb6c1; border-radius: 3px; padding: 4px 6px;
        }
        QSpinBox:focus, QDoubleSpinBox:focus { border-color: #ff69b4; }

        /* ── 滚动条 ── */
        QScrollBar:vertical {
            background: #fff0f5; width: 12px; border: none;
        }
        QScrollBar::handle:vertical {
            background: #ffb6c1; min-height: 30px; border-radius: 6px; margin: 2px;
        }
        QScrollBar::handle:vertical:hover { background: #ff69b4; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal {
            background: #fff0f5; height: 12px; border: none;
        }
        QScrollBar::handle:horizontal {
            background: #ffb6c1; min-width: 30px; border-radius: 6px; margin: 2px;
        }
        QScrollBar::handle:horizontal:hover { background: #ff69b4; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

        /* ── 提示框 ── */
        QToolTip { background-color: #fff0f5; color: #4a2030; border: 1px solid #ff69b4; padding: 4px; }
    )";
    app.setStyleSheet(pinkTheme);

#ifdef RS_WITH_GDAL
    try {
        GDALAllRegister();
        qDebug() << "GDAL initialized.";
    } catch (...) {
        qDebug() << "GDAL initialization failed.";
    }
#endif

    rs::Translation::instance().loadSavedLanguage();

    rs::MainWindow window;
    window.setWindowIcon(app.windowIcon());
    fitMainWindowToScreen(&window);
    window.setVisible(false);

    rs::SplashScreen splash;
    centerOnScreen(&splash);

    // 启动闪屏插画
    splash.setHeroImagePath(QStringLiteral(":/splash_hero.png"));
    QObject::connect(&splash, &rs::SplashScreen::finished, &window, [&window]() {
        window.show();
        window.raise();
        window.activateWindow();
    });

    splash.start(2800);
    return app.exec();
}
