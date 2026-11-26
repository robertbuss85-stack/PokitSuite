#include "pokitbridge.h"

#include <QGuiApplication>
#include <QDir>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QtMath>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PokitSuite Native"));
    QLockFile instanceLock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                           .filePath(QStringLiteral("PokitSuiteNative.lock")));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100)) return 0;
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    PokitBridge controller;
    if (app.arguments().contains(QStringLiteral("--scope-test"))) {
        controller.showScope();
    }
    if (app.arguments().contains(QStringLiteral("--cycle-test"))) {
        QTimer::singleShot(25000, &controller, &PokitBridge::showScope);
        QTimer::singleShot(55000, &controller, &PokitBridge::showMultimeter);
        QTimer::singleShot(75000, &controller, &PokitBridge::showScope);
    }
    if (app.arguments().contains(QStringLiteral("--timebase-test"))) {
        QTimer::singleShot(15000, &controller, &PokitBridge::showScope);
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
    controller.setScriptEngine(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("pokit"), &controller);
    engine.loadFromModule(QStringLiteral("PokitSuite"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 1;
    if (app.arguments().contains(QStringLiteral("--ui-monitor"))) {
        auto *heartbeat = new QTimer(&app);
        auto timing = std::make_shared<QElapsedTimer>();
        timing->start();
        heartbeat->setInterval(1000);
        QObject::connect(heartbeat, &QTimer::timeout, &app, [timing] {
            qInfo() << "UI heartbeat:" << timing->restart() << "ms";
        });
        heartbeat->start();
    }
    if (app.arguments().contains(QStringLiteral("--ui-responsiveness-test"))) {
        auto *heartbeat = new QTimer(&app);
        auto timing = std::make_shared<QElapsedTimer>();
        auto largestGap = std::make_shared<qint64>(0);
        timing->start();
        heartbeat->setInterval(20);
        const auto trace = engine.newArray(1000);
        auto waveform = trace;
        for (int index = 0; index < 1000; ++index)
            waveform.setProperty(index, qSin(index * 1.7123) * 7.0);
        QObject::connect(heartbeat, &QTimer::timeout, &app, [timing, largestGap, waveform, &controller] {
            *largestGap = qMax(*largestGap, timing->restart());
            controller.insert(QStringLiteral("activeFeature"), QStringLiteral("scope"));
            controller.insert(QStringLiteral("scopeSamples"), QVariant::fromValue(waveform));
            controller.insert(QStringLiteral("scopeSamples2"), QVariant::fromValue(waveform));
            emit controller.scopeSamplesChanged();
        });
        heartbeat->start();
        controller.post([](auto *) { QThread::msleep(2000); });
        QTimer::singleShot(2300, &app, [largestGap, &app] {
            qInfo() << "UI heartbeat maximum gap during 2000 ms worker stall:" << *largestGap << "ms";
            app.exit(*largestGap < 250 ? 0 : 2);
        });
    }
    return app.exec();
}

