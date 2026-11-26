#pragma once

#include <QQmlPropertyMap>
#include <QThread>
#include <functional>

class PokitController;
class QJSEngine;

class PokitWorkerThread final : public QThread
{
protected:
    void run() override;
};

// QML only accesses this UI-thread mirror. BLE and file operations stay in the worker.
class PokitBridge final : public QQmlPropertyMap
{
    Q_OBJECT
public:
    explicit PokitBridge(QObject *parent = nullptr);
    ~PokitBridge() override;
    void setScriptEngine(QJSEngine *engine);
    void post(std::function<void(PokitController *)> action);

    Q_INVOKABLE void connectDevice();
    Q_INVOKABLE void meterCommand(const QString &action, int channel, const QVariantMap &options);
    Q_INVOKABLE void saveDeviceColor(const QString &id, const QString &color);
    Q_INVOKABLE void showMultimeter();
    Q_INVOKABLE void showScope();
    Q_INVOKABLE void setMeterMode(int channel, const QString &mode);
    Q_INVOKABLE void setMeterInterval(int milliseconds);
    Q_INVOKABLE void setScopeTrigger(const QString &mode, double level);
    Q_INVOKABLE void setScopeTriggerSource(int channel);
    Q_INVOKABLE void setScopeTriggerPosition(double position);
    Q_INVOKABLE void setScopeConfig(const QString &coupling, double voltsPerDiv, int timePerDivUs);
    Q_INVOKABLE void setScopeRunning(bool running);
    Q_INVOKABLE void scopeSingle();
    Q_INVOKABLE void saveScopeCapture();
    Q_INVOKABLE void loadScopeCapture(const QString &path);
    Q_INVOKABLE void clearScopeReference();
    Q_INVOKABLE void openCaptureFolder();

signals:
    void meterHistoryChanged();
    void scopeSamplesChanged();
    void referenceChanged();

private:
    void applySnapshot(const QVariantMap &values);
    PokitWorkerThread m_worker;
    QObject *m_context = nullptr;
    PokitController *m_controller = nullptr;
    QJSEngine *m_scriptEngine = nullptr;
    QVariantMap m_values;
};
