#include <QApplication>
#include <FelgoApplication>
#include <QQmlApplicationEngine>

#include "cvfilter.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);

    FelgoApplication felgo;

    // Use platform-specific fonts instead of Felgo's default font
    felgo.setPreservePlatformFonts(true);

    QQmlApplicationEngine engine;
    felgo.initialize(&engine);

    CVFilter::registerQMLType();

    felgo.setMainQmlFileName(QStringLiteral("qrc:/qml/Main.qml"));

    engine.load(QUrl(felgo.mainQmlFileName()));

    return app.exec();
}
