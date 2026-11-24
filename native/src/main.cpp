#include "pokitcontroller.h"

#include <QGuiApplication>
#include <QDir>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PokitSuite Native"));
    QLockFile instanceLock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                           .filePath(QStringLiteral("PokitSuiteNative.lock")));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100)) return 0;
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    PokitController controller;
    if (app.arguments().contains(QStringLiteral("--scope-test"))) {
        controller.showScope();
    }
    if (app.arguments().contains(QStringLiteral("--cycle-test"))) {
        QTimer::singleShot(25000, &controller, &PokitController::showScope);
        QTimer::singleShot(55000, &controller, &PokitController::showMultimeter);
        QTimer::singleShot(75000, &controller, &PokitController::showScope);
    }
    if (app.arguments().contains(QStringLiteral("--timebase-test"))) {
        QTimer::singleShot(15000, &controller, &PokitController::showScope);
        QTimer::singleShot(25000, &controller, [&controller] {
            controller.setScopeConfig(QStringLiteral("DC"), 1.0, 100); // 100 µs/div
        });
        QTimer::singleShot(45000, &controller, [&controller] {
            controller.setScopeConfig(QStringLiteral("DC"), 1.0, 1000); // 1 ms/div
        });
        QTimer::singleShot(65000, &controller, [&controller] {
            controller.setScopeConfig(QStringLiteral("DC"), 1.0, 10000); // 10 ms/div
        });
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pokit"), &controller);
    engine.loadFromModule(QStringLiteral("PokitSuite"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}

