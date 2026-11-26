#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include "metertools.h"

#include <qtpokit/dsoservice.h>
#include <qtpokit/multimeterservice.h>
#include <qtpokit/statusservice.h>

class PokitDevice;
class PokitDiscoveryAgent;
class QBluetoothDeviceInfo;
class QJsonObject;

class PokitController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceName2 READ deviceName2 NOTIFY deviceChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString deviceColor READ deviceColor NOTIFY devicesChanged)
    Q_PROPERTY(QString deviceColor2 READ deviceColor2 NOTIFY devicesChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool connected2 READ connected2 NOTIFY connectedChanged)
    Q_PROPERTY(int connectedDeviceCount READ connectedDeviceCount NOTIFY connectedChanged)
    Q_PROPERTY(double meterValue READ meterValue NOTIFY meterValueChanged)
    Q_PROPERTY(double meterValue2 READ meterValue2 NOTIFY meterValueChanged)
    Q_PROPERTY(QString meterUnit READ meterUnit NOTIFY meterValueChanged)
    Q_PROPERTY(QString meterUnit2 READ meterUnit2 NOTIFY meterValueChanged)
    Q_PROPERTY(bool meterValid MEMBER m_meterValid NOTIFY meterValueChanged)
    Q_PROPERTY(bool meterValid2 MEMBER m_meterValid2 NOTIFY meterValueChanged)
    Q_PROPERTY(bool meterOverrange MEMBER m_meterOverrange NOTIFY meterValueChanged)
    Q_PROPERTY(bool meterOverrange2 MEMBER m_meterOverrange2 NOTIFY meterValueChanged)
    Q_PROPERTY(QString meterSwitch READ meterSwitch NOTIFY meterValueChanged)
    Q_PROPERTY(QString meterSwitch2 READ meterSwitch2 NOTIFY meterValueChanged)
    Q_PROPERTY(QVariantList meterHistory READ meterHistory NOTIFY meterHistoryChanged)
    Q_PROPERTY(QVariantList meterHistory2 READ meterHistory2 NOTIFY meterHistoryChanged)
    Q_PROPERTY(QVariantList scopeSamples READ scopeSamples NOTIFY scopeSamplesChanged)
    Q_PROPERTY(QVariantList scopeSamples2 READ scopeSamples2 NOTIFY scopeSamplesChanged)
    Q_PROPERTY(bool scopeRunning READ scopeRunning NOTIFY scopeStateChanged)
    Q_PROPERTY(QString scopeTriggerMode MEMBER m_triggerMode NOTIFY scopeStateChanged)
    Q_PROPERTY(double scopeTriggerLevel MEMBER m_triggerLevel NOTIFY scopeStateChanged)
    Q_PROPERTY(double scopeVoltsPerDiv MEMBER m_scopeVoltsPerDiv NOTIFY scopeStateChanged)
    Q_PROPERTY(int scopeTimePerDivUs MEMBER m_scopeTimePerDivUs NOTIFY scopeStateChanged)
    Q_PROPERTY(QString scopeCoupling MEMBER m_scopeCoupling NOTIFY scopeStateChanged)
    Q_PROPERTY(int scopeTriggerSource READ scopeTriggerSource NOTIFY scopeStateChanged)
    Q_PROPERTY(bool scopeRfReady READ scopeRfReady NOTIFY scopeStateChanged)
    Q_PROPERTY(bool scopeRfPair READ scopeRfPair NOTIFY scopeStateChanged)
    Q_PROPERTY(int scopeRfTriggerSample READ scopeRfTriggerSample NOTIFY scopeStateChanged)
    Q_PROPERTY(int scopeRfTriggerSample2 READ scopeRfTriggerSample2 NOTIFY scopeStateChanged)
    Q_PROPERTY(int scopeRfWindowUs READ scopeRfWindowUs NOTIFY scopeStateChanged)
    Q_PROPERTY(double scopeRfTriggerFraction READ scopeRfTriggerFraction NOTIFY scopeStateChanged)
    Q_PROPERTY(double scopeSampleRate2 READ scopeSampleRate2 NOTIFY scopeStateChanged)
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
    Q_PROPERTY(double scopeVpp2 READ scopeVpp2 NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeMinimum2 READ scopeMinimum2 NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeMaximum2 READ scopeMaximum2 NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeRms2 READ scopeRms2 NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeFrequency2 READ scopeFrequency2 NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopePeriod2 READ scopePeriod2 NOTIFY scopeMetricsChanged)
    Q_PROPERTY(double scopeCycles2 READ scopeCycles2 NOTIFY scopeMetricsChanged)
    Q_PROPERTY(QVariantList savedCaptures READ savedCaptures NOTIFY savedCapturesChanged)
    Q_PROPERTY(QVariantList referenceSamples READ referenceSamples NOTIFY referenceChanged)
    Q_PROPERTY(QString referenceName READ referenceName NOTIFY referenceChanged)
    Q_PROPERTY(double referenceSampleRate MEMBER m_referenceSampleRate NOTIFY referenceChanged)
    Q_PROPERTY(int referenceTriggerSample MEMBER m_referenceTriggerSample NOTIFY referenceChanged)
    Q_PROPERTY(bool referenceHasTrigger MEMBER m_referenceHasTrigger NOTIFY referenceChanged)
    Q_PROPERTY(QString activeFeature READ activeFeature NOTIFY activeFeatureChanged)
    Q_PROPERTY(QVariantMap meterTools READ meterTools NOTIFY meterToolsChanged)

public:
    explicit PokitController(QObject *parent = nullptr);
    QString status() const { return m_status; }
    QString deviceName() const { return m_deviceName; }
    QString deviceName2() const { return m_deviceName2; }
    QVariantList devices() const;
    QString deviceColor() const;
    QString deviceColor2() const;
    bool connected() const { return m_connected; }
    bool connected2() const { return m_connected2; }
    int connectedDeviceCount() const { return int(m_connected) + int(m_connected2); }
    double meterValue() const { return m_meterValue; }
    double meterValue2() const { return m_meterValue2; }
    QString meterUnit() const { return m_meterUnit; }
    QString meterUnit2() const { return m_meterUnit2; }
    QString meterSwitch() const;
    QString meterSwitch2() const;
    QVariantList meterHistory() const;
    QVariantList meterHistory2() const;
    QVariantList scopeSamples() const;
    QVariantList scopeSamples2() const;
    bool scopeRunning() const { return m_scopeRunning; }
    int scopeTriggerSource() const { return m_scopeTriggerSource; }
    bool scopeRfReady() const;
    bool scopeRfPair() const { return m_scopeRfPair; }
    int scopeRfTriggerSample() const { return m_scopeMetadata.triggerSample.value_or(0); }
    int scopeRfTriggerSample2() const { return m_scopeMetadata2.triggerSample.value_or(0); }
    double scopeRfTriggerFraction() const {
        return m_rfSettings.numberOfSamples
            ? double(m_scopeRfTriggerSample) / m_rfSettings.numberOfSamples : 0.0;
    }
    int scopeRfWindowUs() const { return int(m_rfSettings.samplingWindow); }
    double scopeSampleRate() const { return m_scopeMetadata.samplingRate; }
    double scopeSampleRate2() const { return m_scopeMetadata2.samplingRate; }
    double scopeVpp() const { return m_scopeVpp; }
    double scopeMinimum() const { return m_scopeMinimum; }
    double scopeMaximum() const { return m_scopeMaximum; }
    double scopeMean() const { return m_scopeMean; }
    double scopeRms() const { return m_scopeRms; }
    double scopeFrequency() const { return m_scopeFrequency; }
    double scopePeriod() const { return m_scopePeriod; }
    double scopeCycles() const { return m_scopeCycles; }
    double scopeDutyCycle() const { return m_scopeDutyCycle; }
    double scopeVpp2() const { return m_scopeVpp2; }
    double scopeMinimum2() const { return m_scopeMinimum2; }
    double scopeMaximum2() const { return m_scopeMaximum2; }
    double scopeRms2() const { return m_scopeRms2; }
    double scopeFrequency2() const { return m_scopeFrequency2; }
    double scopePeriod2() const { return m_scopePeriod2; }
    double scopeCycles2() const { return m_scopeCycles2; }
    QVariantList savedCaptures() const { return m_savedCaptures; }
    QVariantList referenceSamples() const;
    QString referenceName() const { return m_referenceName; }
    QString activeFeature() const { return m_feature; }
    QVariantMap meterTools() const { return m_meterTools.state(); }
    Q_INVOKABLE void meterCommand(const QString &action, int channel, const QVariantMap &options = {}) { m_meterTools.command(action, channel, options); }

    Q_INVOKABLE void connectDevice();
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
    Q_INVOKABLE QString saveScopeCapture();
    Q_INVOKABLE void loadScopeCapture(const QString &jsonPath);
    Q_INVOKABLE void clearScopeReference();
    Q_INVOKABLE void openCaptureFolder();

signals:
    void statusChanged();
    void deviceChanged();
    void devicesChanged();
    void connectedChanged();
    void meterValueChanged();
    void meterHistoryChanged();
    void scopeSamplesChanged();
    void scopeStateChanged();
    void scopeMetricsChanged();
    void savedCapturesChanged();
    void referenceChanged();
    void activeFeatureChanged();
    void meterToolsChanged();

private:
    MeterTools m_meterTools;
    void setStatus(const QString &status);
    void attachDevice(const QBluetoothDeviceInfo &info);
    void attachSecondDevice(const QBluetoothDeviceInfo &info);
    void activateCurrentFeature();
    void ensureMeter();
    void ensureDso();
    void ensureMeter2();
    void ensureMeterStatus(bool second);
    void configureMeter(bool second);
    void ensureDso2();
    void startMeter();
    void startScope();
    void startMeter2();
    void startScope2();
    DsoService::Settings scopeSettings() const;
    void startRfCapture();
    void stopRfCapture();
    void clearRfPair();
    void failRfCapture(const QString &message);
    void onRfSettingsWritten(int channel, DsoService::SyncRole role, quint8 group);
    void onRfMetadata(int channel, const DsoService::Metadata &metadata);
    void onRfSamples(int channel, const DsoService::Samples &samples);
    void updateScopeMetrics();
    void updateScopeMetrics2();
    void publishAutomationState();
    void processAutomationCommands();
    void scheduleReconnect();
    void scheduleReconnect2();
    void refreshSavedCaptures();
    QString captureStoragePath() const;
    bool writeCapturePdf(const QString &path, const QJsonObject &capture) const;

    PokitDiscoveryAgent *m_discovery = nullptr;
    PokitDevice *m_device = nullptr;
    PokitDevice *m_device2 = nullptr;
    MultimeterService *m_meter = nullptr;
    MultimeterService *m_meter2 = nullptr;
    DsoService *m_dso = nullptr;
    DsoService *m_dso2 = nullptr;
    QTimer m_reconnectTimer;
    QTimer m_scanRetry;
    QTimer m_reconnectTimer2;
    QTimer m_meterWatchdog;
    QTimer m_meterWatchdog2;
    QTimer m_meterPoll;
    QTimer m_scopeWatchdog;
    QTimer m_scopeWatchdog2;
    QTimer m_scopeRestartTimer;
    QTimer m_scopeRestartTimer2;
    QTimer m_rfTimeout;
    QTimer m_rfRestart;
    QTimer m_automationTimer;
    QString m_status = QStringLiteral("Bereit");
    QString m_deviceName = QStringLiteral("Nicht verbunden");
    QString m_deviceName2 = QStringLiteral("Nicht verbunden");
    QString m_deviceId;
    QString m_deviceId2;
    QVariantMap m_savedDevices;
    QString m_feature = QStringLiteral("meter");
    QString m_meterUnit = QStringLiteral("V DC");
    QString m_meterUnit2 = QStringLiteral("V DC");
    StatusService *m_meterStatus[2]{};
    std::optional<StatusService::SwitchPosition> m_meterSwitchPosition[2];
    MultimeterService::Mode m_configuredMeterMode[2]{};
    bool m_meterValid = false;
    bool m_meterValid2 = false;
    bool m_meterOverrange = false;
    bool m_meterOverrange2 = false;
    bool m_meterStatusReady[2]{};
    QString m_meterModeName[2]{QStringLiteral("DC Voltage"), QStringLiteral("DC Voltage")};
    QString m_triggerMode = QStringLiteral("free");
    QString m_scopeCoupling = QStringLiteral("DC");
    bool m_connected = false;
    bool m_connected2 = false;
    bool m_meterReady = false;
    bool m_meterReady2 = false;
    bool m_dsoReady = false;
    bool m_dsoReady2 = false;
    bool m_scopeAccepting = false;
    bool m_scopeAccepting2 = false;
    bool m_scopeRunning = true;
    bool m_scopeSingle = false;
    enum class RfStage { Idle, ArmingFollower, WaitingForFrames };
    struct RfFrame {
        std::optional<DsoService::Metadata> metadata;
        QVector<double> values;
    };
    RfStage m_rfStage = RfStage::Idle;
    RfFrame m_rfFrames[2];
    DsoService::Settings m_rfSettings{};
    int m_scopeTriggerSource = 0;
    double m_scopeTriggerPosition = 0.5;
    bool m_rfNotificationsReady[2]{};
    bool m_scopeRfPair = false;
    bool m_rfContinuous = false;
    int m_rfResends = 0;
    int m_rfRecoveries = 0;
    quint8 m_rfGroup = 0;
    quint16 m_scopeRfTriggerSample = 0;
    double m_meterValue = 0.0;
    double m_meterValue2 = 0.0;
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
    double m_scopeVpp2 = 0.0;
    double m_scopeMinimum2 = 0.0;
    double m_scopeMaximum2 = 0.0;
    double m_scopeMean2 = 0.0;
    double m_scopeRms2 = 0.0;
    double m_scopeFrequency2 = 0.0;
    double m_scopePeriod2 = 0.0;
    double m_scopeCycles2 = 0.0;
    double m_scopeDutyCycle2 = 0.0;
    int m_scopeTimePerDivUs = 10000;
    quint32 m_expectedScopeWindowUs = 100000;
    quint16 m_expectedScopeSamples = 1000;
    quint32 m_expectedScopeWindowUs2 = 100000;
    quint16 m_expectedScopeSamples2 = 1000;
    int m_scopeStableFrames = 0;
    int m_scopeStableFrames2 = 0;
    int m_meterInterval = 500;
    DsoService::Metadata m_scopeMetadata{};
    DsoService::Metadata m_scopeMetadata2{};
    QVector<double> m_meterValues;
    QVector<double> m_meterValues2;
    QVector<double> m_scopeValues;
    QVector<double> m_scopeValues2;
    QVector<double> m_scopePendingValues;
    QVector<double> m_scopePendingValues2;
    QVariantList m_savedCaptures;
    QVector<double> m_referenceValues;
    QString m_referenceName;
    double m_referenceSampleRate = 0.0;
    int m_referenceTriggerSample = 0;
    bool m_referenceHasTrigger = false;
};

