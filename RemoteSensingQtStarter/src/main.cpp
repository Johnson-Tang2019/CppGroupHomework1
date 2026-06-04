#include "rs/MainWindow.h"

#include <QApplication>
#include <QDebug>

#ifdef RS_WITH_GDAL
#include <gdal_priv.h>
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

#ifdef RS_WITH_GDAL
    try {
        GDALAllRegister();
        qDebug() << "GDAL initialized.";
    } catch (...) {
        qDebug() << "GDAL initialization failed, continuing without aborting UI startup.";
    }
#else
    qDebug() << "GDAL is not enabled in this build.";
#endif

    rs::MainWindow window;
    window.resize(1200, 760);
    window.show();
    return app.exec();
}
