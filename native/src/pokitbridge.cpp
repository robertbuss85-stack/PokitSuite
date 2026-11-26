#include "pokitbridge.h"
#include "pokitcontroller.h"

#include <QMetaProperty>
#include <QTimer>
#include <QCoreApplication>
#include <QEvent>
#include <QJSEngine>
#include <QElapsedTimer>
#include <QDebug>
#ifdef Q_OS_WIN
#include <objbase.h>
#endif

void PokitWorkerThread::run()
{
#ifdef Q_OS_WIN
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    QThread::run();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
#ifdef Q_OS_WIN
    if (SUCCEEDED(initialized)) CoUninitialize();
#endif
}

static QVariantMap snapshot(PokitController *controller)
{
    QVariantMap values;
    const auto *meta = controller->metaObject();
    for (int index = meta->propertyOffset(); index < meta->propertyCount(); ++index) {
        const auto property = meta->property(index);
        values.insert(QString::fromLatin1(property.name()), property.read(controller));
    }
    return values;
}

PokitBridge::PokitBridge(QObject *parent) : QQmlPropertyMap(this, parent)
{
    m_context = new QObject;
    m_context->moveToThread(&m_worker);
    connect(&m_worker, &QThread::finished, m_context, &QObject::deleteLater);
    m_worker.start();
    QVariantMap initial;
    QMetaObject::invokeMethod(m_context, [this, &initial] {
        m_controller = new PokitController(m_context);
        initial = snapshot(m_controller);
        auto *refresh = new QTimer(m_context);
        refresh->setSingleShot(true);
        refresh->setInterval(50);
        const auto schedule = [refresh] {
            if (!refresh->isActive()) refresh->start();
        };
        connect(m_controller, &PokitController::statusChanged, refresh, schedule);
        connect(m_controller, &PokitController::deviceChanged, refresh, schedule);
        connect(m_controller, &PokitController::devicesChanged, refresh, schedule);
        connect(m_controller, &PokitController::connectedChanged, refresh, schedule);
        connect(m_controller, &PokitController::meterValueChanged, refresh, schedule);
        connect(m_controller, &PokitController::meterHistoryChanged, refresh, schedule);
        connect(m_controller, &PokitController::scopeSamplesChanged, refresh, schedule);
        connect(m_controller, &PokitController::scopeStateChanged, refresh, schedule);
        connect(m_controller, &PokitController::scopeMetricsChanged, refresh, schedule);
        connect(m_controller, &PokitController::savedCapturesChanged, refresh, schedule);
        connect(m_controller, &PokitController::referenceChanged, refresh, schedule);
        connect(m_controller, &PokitController::activeFeatureChanged, refresh, schedule);
        connect(refresh, &QTimer::timeout, m_context, [this] {
            const auto values = snapshot(m_controller);
            QMetaObject::invokeMethod(this, [this, values] { applySnapshot(values); },
                                      Qt::QueuedConnection);
        });
    }, Qt::BlockingQueuedConnection);
    applySnapshot(initial);
}

PokitBridge::~PokitBridge()
{
    m_worker.quit();
    m_worker.wait();
}

void PokitBridge::applySnapshot(const QVariantMap &values)
{
    QElapsedTimer updateTime;
    updateTime.start();
    const bool meterChanged = m_values.value(QStringLiteral("meterHistory")) != values.value(QStringLiteral("meterHistory"))
        || m_values.value(QStringLiteral("meterHistory2")) != values.value(QStringLiteral("meterHistory2"));
    const bool scopeChanged = m_values.value(QStringLiteral("scopeSamples")) != values.value(QStringLiteral("scopeSamples"))
        || m_values.value(QStringLiteral("scopeSamples2")) != values.value(QStringLiteral("scopeSamples2"));
    const bool referenceChanged = m_values.value(QStringLiteral("referenceSamples")) != values.value(QStringLiteral("referenceSamples"));
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (m_values.value(it.key()) == it.value()) continue;
        // Give Canvas an owned JavaScript array. A QVariantList property is a
        // reference sequence, which can re-read/copy the whole list per sample.
        if (m_scriptEngine && it->metaType().id() == QMetaType::QVariantList) {
            const auto list = it->toList();
            auto array = m_scriptEngine->newArray(uint(list.size()));
            for (qsizetype index = 0; index < list.size(); ++index)
                array.setProperty(uint(index), m_scriptEngine->toScriptValue(list.at(index)));
            insert(it.key(), QVariant::fromValue(array));
        } else {
            insert(it.key(), it.value());
        }
    }
    m_values = values;
    if (meterChanged) emit meterHistoryChanged();
    if (scopeChanged) emit scopeSamplesChanged();
    if (referenceChanged) emit this->referenceChanged();
    if (updateTime.elapsed() > 100) qInfo() << "UI snapshot took" << updateTime.elapsed() << "ms";
}

void PokitBridge::setScriptEngine(QJSEngine *engine)
{
    m_scriptEngine = engine;
    const auto initial = m_values;
    m_values.clear();
    applySnapshot(initial);
}

void PokitBridge::post(std::function<void(PokitController *)> action)
{
    QMetaObject::invokeMethod(m_context, [this, action = std::move(action)] { action(m_controller); },
                              Qt::QueuedConnection);
}

void PokitBridge::connectDevice() { post([](auto *c) { c->connectDevice(); }); }
void PokitBridge::showMultimeter() { post([](auto *c) { c->showMultimeter(); }); }
void PokitBridge::showScope() { post([](auto *c) { c->showScope(); }); }
void PokitBridge::scopeSingle() { post([](auto *c) { c->scopeSingle(); }); }
void PokitBridge::saveScopeCapture() { post([](auto *c) { c->saveScopeCapture(); }); }
void PokitBridge::clearScopeReference() { post([](auto *c) { c->clearScopeReference(); }); }
void PokitBridge::openCaptureFolder() { post([](auto *c) { c->openCaptureFolder(); }); }
void PokitBridge::saveDeviceColor(const QString &id, const QString &color)
{ post([id, color](auto *c) { c->saveDeviceColor(id, color); }); }
void PokitBridge::setMeterMode(int channel, const QString &mode)
{ post([channel, mode](auto *c) { c->setMeterMode(channel, mode); }); }
void PokitBridge::setMeterInterval(int milliseconds)
{ post([milliseconds](auto *c) { c->setMeterInterval(milliseconds); }); }
void PokitBridge::setScopeTrigger(const QString &mode, double level)
{ post([mode, level](auto *c) { c->setScopeTrigger(mode, level); }); }
void PokitBridge::setScopeTriggerSource(int channel)
{ post([channel](auto *c) { c->setScopeTriggerSource(channel); }); }
void PokitBridge::setScopeTriggerPosition(double position)
{ post([position](auto *c) { c->setScopeTriggerPosition(position); }); }
void PokitBridge::setScopeConfig(const QString &coupling, double voltsPerDiv, int timePerDivUs)
{ post([coupling, voltsPerDiv, timePerDivUs](auto *c) { c->setScopeConfig(coupling, voltsPerDiv, timePerDivUs); }); }
void PokitBridge::setScopeRunning(bool running)
{ post([running](auto *c) { c->setScopeRunning(running); }); }
void PokitBridge::loadScopeCapture(const QString &path)
{ post([path](auto *c) { c->loadScopeCapture(path); }); }
