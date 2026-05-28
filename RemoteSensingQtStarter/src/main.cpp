#include "rs/MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    rs::MainWindow window;
    window.resize(1200, 760);
    window.show();

    return app.exec();
}
