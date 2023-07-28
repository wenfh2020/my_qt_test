#include <QtWidgets/QApplication>

#include "TestApp.h"

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication a(argc, argv);
    TestApp w;
    w.init();
    w.show();
    return a.exec();
}
