#include "rs/MainWindow.h"
#include "rs/SplashScreen.h"
#include "rs/AppTheme.h"
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

#ifdef RS_WITH_GDAL
    try {
        GDALAllRegister();
        qDebug() << "GDAL initialized.";
    } catch (...) {
        qDebug() << "GDAL initialization failed.";
    }
#endif

    rs::Translation::instance().loadSavedLanguage();
    rs::AppTheme::instance().loadSavedTheme();
    rs::AppTheme::instance().applyToApplication(&app);

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
