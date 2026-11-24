#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <qtpokit/dsoservice.h>
#include <qtpokit/multimeterservice.h>

class PokitDevice;
class PokitDiscoveryAgent;
class QBluetoothDeviceInfo;

class PokitController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(double meterValue READ meterValue NOTIFY meterValueChanged)
    Q_PROPERTY(QString meterUnit READ meterUnit NOTIFY meterValueChanged)
    Q_PROPERTY(QVariantList meterHistory READ meterHistory NOTIFY meterHistoryChanged)
    Q_PROPERTY(QVariantList scopeSamples READ scopeSamples NOTIFY scopeSamplesChanged)
    Q_PROPERTY(bool scopeRunning READ scopeRunning NOTIFY scopeStateChanged)
    Q_PROPERTY(double scopeSampleRate READ scopeSampleRate NOTIFY scopeStateChanged)
    Q_PROPERTY(double scopeVpp READ scopeVpp NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeMinimum READ scopeMinimum NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeMaximum READ scopeMaximum NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeMean READ scopeMean NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeRms READ scopeRms NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeFrequency READ scopeFrequency NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopePeriod READ scopePeriod NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeCycles READ scopeCycles NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeDutyCycle READ scopeDutyCycle NOTIFY scopeMetricsChanged)
    Q_PROPERTY(QString activeFeature READ activeFeature NOTIFY activeFeatureChanged)

public:
    explicit PokitController(QObject *parent = nullptr);
    QString status() const { return m_status; }
    QString deviceName() const { return m_deviceName; }
    bool connected() const { return m_connected; }
    double meterValue() const { return m_meterValue; }
    QString meterUnit() const { return m_meterUnit; }
    QVariantList meterHistory() const;
    QVariantList scopeSamples() const;
    bool scopeRunning() const { return m_scopeRunning; }
    double scopeSampleRate() const { return m_scopeMetadata.samplingRate; }
    double scopeVpp() const { return m_scopeVpp; }
    double scopeMinimum() const { return m_scopeMinimum; }
    double scopeMaximum() const { return m_scopeMaximum; }
    double scopeMean() const { return m_scopeMean; }
    double scopeRms() const { return m_scopeRms; }
    double scopeFrequency() const { return m_scopeFrequency; }
    double scopePeriod() const { return m_scopePeriod; }
    double scopeCycles() const { return m_scopeCycles; }
    double scopeDutyCycle() const { return m_scopeDutyCycle; }
    QString activeFeature() const { return m_feature; }

    Q_INVOKABLE void connectDevice();
    Q_INVOKABLE void showMultimeter();
    Q_INVOKABLE void showScope();
    Q_INVOKABLE void setMeterMode(const QString &mode);
    Q_INVOKABLE void setMeterInterval(int milliseconds);
    Q_INVOKABLE void setScopeTrigger(const QString &mode, double level);
    Q_INVOKABLE void setScopeConfig(const QString &coupling, double voltsPerDiv, int timePerDivUs);
    Q_INVOKABLE void setScopeRunning(bool running);
    Q_INVOKABLE void scopeSingle();

signals:
    void statusChanged();
    void deviceChanged();
    void connectedChanged();
    void meterValueChanged();
    void meterHistoryChanged();
    void scopeSamplesChanged();
    void scopeStateChanged();
    void scopeMetricsChanged();
    void activeFeatureChanged();

private:
    void setStatus(const QString &status);
    void attachDevice(const QBluetoothDeviceInfo &info);
    void activateCurrentFeature();
    void ensureMeter();
    void ensureDso();
    void startMeter();
    void startScope();
    void updateScopeMetrics();
    void publishAutomationState();
    void processAutomationCommands();
    void scheduleReconnect();

    PokitDiscoveryAgent *m_discovery = nullptr;
    PokitDevice *m_device = nullptr;
    MultimeterService *m_meter = nullptr;
    DsoService *m_dso = nullptr;
    QTimer m_reconnectTimer;
    QTimer m_meterWatchdog;
    QTimer m_scopeWatchdog;
    QTimer m_scopeRestartTimer;
    QTimer m_automationTimer;
    QString m_status = QStringLiteral("Bereit");
    QString m_deviceName = QStringLiteral("Nicht verbunden");
    QString m_feature = QStringLiteral("meter");
    QString m_meterUnit = QStringLiteral("V DC");
    QString m_meterModeName = QStringLiteral("DC Voltage");
    QString m_triggerMode = QStringLiteral("free");
    QString m_scopeCoupling = QStringLiteral("DC");
    bool m_connected = false;
    bool m_meterReady = false;
    bool m_dsoReady = false;
    bool m_scopeAccepting = false;
    bool m_scopeRunning = true;
    bool m_scopeSingle = false;
    double m_meterValue = 0.0;
    double m_triggerLevel = 0.0;
    double m_scopeVoltsPerDiv = 1.0;
    double m_scopeVpp = 0.0;
    double m_scopeMinimum = 0.0;
    double m_scopeMaximum = 0.0;
    double m_scopeMean = 0.0;
    double m_scopeRms = 0.0;
    double m_scopeFrequency = 0.0;
    double m_scopePeriod = 0.0;
    double m_scopeCycles = 0.0;
    double m_scopeDutyCycle = 0.0;
    int m_scopeTimePerDivUs = 10000;
    quint32 m_expectedScopeWindowUs = 100000;
    quint16 m_expectedScopeSamples = 1000;
    int m_scopeStableFrames = 0;
    int m_meterInterval = 500;
    DsoService::Metadata m_scopeMetadata{};
    QVector<double> m_meterValues;
    QVector<double> m_scopeValues;
    QVector<double> m_scopePendingValues;
};

