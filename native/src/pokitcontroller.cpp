#include "pokitcontroller.h"
#include "scopeexport.h"

#include <qtpokit/abstractpokitservice.h>
#include <qtpokit/pokitdevice.h>
#include <qtpokit/pokitdiscoveryagent.h>
#include <qtpokit/pokitpro.h>

#include <QBluetoothDeviceInfo>
#include <QDebug>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLowEnergyController>
#include <QSaveFile>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <cmath>

static QDir automationRoot()
{
    const QDir app(QCoreApplication::applicationDirPath());
    for(const QString &path:{app.absolutePath(),app.absoluteFilePath(QStringLiteral("../..")),QDir::currentPath()}) {
        QDir candidate(path);
        if(candidate.exists(QStringLiteral("mcp/pokitsuite_mcp.py")))return candidate;
    }
    return app;
}

static QString deviceKey(const QBluetoothDeviceInfo &info)
{
    if (!info.deviceUuid().isNull())
        return info.deviceUuid().toString(QUuid::WithoutBraces);
    if (!info.address().isNull()) return info.address().toString();
    return {};
}

PokitController::PokitController(QObject *parent) : QObject(parent)
{
    m_switchClock.start();
    qInfo()<<"PokitSuite telemetry directory:"<<automationRoot().absolutePath();
    connect(&m_meterTools, &MeterTools::changed, this, &PokitController::meterToolsChanged);
    m_savedDevices = QSettings(QStringLiteral("PokitSuite"), QStringLiteral("PokitSuite"))
                         .value(QStringLiteral("devices")).toMap();
    m_savedDevices.remove(QUuid{}.toString(QUuid::WithoutBraces));
    connect(this, &PokitController::deviceChanged, this, &PokitController::devicesChanged);
    connect(this, &PokitController::connectedChanged, this, &PokitController::devicesChanged);
    connect(this, &PokitController::connectedChanged, this, &PokitController::publishAutomationState);
    m_discovery = new PokitDiscoveryAgent(this);
    m_discovery->setLowEnergyDiscoveryTimeout(12000);
    connect(m_discovery, &PokitDiscoveryAgent::pokitDeviceDiscovered,
            this, &PokitController::attachDevice);
    connect(m_discovery, &PokitDiscoveryAgent::pokitDeviceUpdated, this,
            [this](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { attachDevice(info); });
    const auto checkName = [this](const QBluetoothDeviceInfo &info) {
        if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)) return;
        const QString name = info.name().simplified().remove(u' ').toLower();
        if (name == QStringLiteral("pokitpro") || name == QStringLiteral("pokitmeter"))
            attachDevice(info);
    };
    connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, checkName);
    connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this, checkName);
    m_scanRetry.setSingleShot(true);
    m_scanRetry.setInterval(4000);
    connect(&m_scanRetry, &QTimer::timeout, this, &PokitController::connectDevice);
    connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, [this] {
        qWarning().noquote() << "PokitSuite: BLE-Suche:" << m_discovery->errorString();
        setStatus(QStringLiteral("Bluetooth-Suche: %1").arg(m_discovery->errorString()));
        if (!m_device2) m_scanRetry.start();
    });
    connect(m_discovery, &PokitDiscoveryAgent::finished, this, [this] {
        if (!m_device) {
            setStatus(QStringLiteral("Kein Pokit gefunden – neuer Suchlauf …"));
            m_scanRetry.start();
        } else if (!m_device2) {
            setStatus(QStringLiteral("CH1 aktiv – zweites Pokit wird weiter gesucht …"));
            // Pokits may sleep between advertising windows. Keep scanning in
            // short bursts while CH1 continues measuring, so CH2 is attached
            // as soon as it wakes without requiring a button press.
            m_scanRetry.start();
        }
    });
    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(3000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_device && !m_connected) {
            setStatus(QStringLiteral("Verbindung wird wiederhergestellt …"));
            m_device->controller()->connectToDevice();
        }
    });
    m_reconnectTimer2.setSingleShot(true);
    m_reconnectTimer2.setInterval(3000);
    connect(&m_reconnectTimer2, &QTimer::timeout, this, [this] {
        if (m_device2 && !m_connected2) m_device2->controller()->connectToDevice();
    });
    m_scopeWatchdog.setSingleShot(true);
    m_scopeWatchdog.setInterval(6000);
    connect(&m_scopeWatchdog, &QTimer::timeout, this, [this] {
        if (!m_connected || !m_dsoReady || !m_scopeRunning ||
            m_feature != QStringLiteral("scope")) return;
        setStatus(QStringLiteral("Scope-Daten fehlen – Kanal wird neu synchronisiert …"));
        m_dso->enableMetadataNotifications();
        QTimer::singleShot(350, this, [this] {
            if (!m_connected || m_feature != QStringLiteral("scope")) return;
            m_dso->enableReadingNotifications();
            QTimer::singleShot(700, this, [this] { startScope(); });
        });
    });
    m_scopeRestartTimer.setSingleShot(true);
    m_scopeRestartTimer.setInterval(150);
    connect(&m_scopeRestartTimer, &QTimer::timeout,
            this, &PokitController::startScope);
    m_scopeWatchdog2.setSingleShot(true);
    m_scopeWatchdog2.setInterval(6000);
    connect(&m_scopeWatchdog2, &QTimer::timeout, this, [this] {
        if (!m_connected2 || !m_dsoReady2 || !m_scopeRunning ||
            m_feature != QStringLiteral("scope")) return;
        setStatus(QStringLiteral("CH2 Scope-Daten fehlen – Kanal wird neu synchronisiert …"));
        m_dso2->enableMetadataNotifications();
        QTimer::singleShot(350, this, [this] {
            if (!m_connected2 || m_feature != QStringLiteral("scope")) return;
            m_dso2->enableReadingNotifications();
            QTimer::singleShot(700, this, [this] { startScope2(); });
        });
    });
    m_scopeRestartTimer2.setSingleShot(true);
    m_scopeRestartTimer2.setInterval(150);
    connect(&m_scopeRestartTimer2, &QTimer::timeout,
            this, &PokitController::startScope2);
    m_rfTimeout.setSingleShot(true);
    m_rfRestart.setSingleShot(true);
    m_rfRestart.setInterval(500);
    connect(&m_rfRestart, &QTimer::timeout, this, &PokitController::startRfCapture);
    connect(&m_rfTimeout, &QTimer::timeout, this, [this] {
        const auto incomplete = [](const RfFrame &frame) {
            return frame.metadata && frame.values.size() < frame.metadata->numberOfSamples;
        };
        if (m_rfStage == RfStage::WaitingForFrames && m_connected && m_connected2
            && (incomplete(m_rfFrames[0]) || incomplete(m_rfFrames[1])) && m_rfResends < 2) {
            ++m_rfResends;
            qInfo() << "RF retransmission" << m_rfResends
                    << "received samples" << m_rfFrames[0].values.size() << m_rfFrames[1].values.size();
            setStatus(QStringLiteral("Funktrigger: fehlende Daten werden erneut abgerufen"));
            m_rfTimeout.start(15000);
            for (int channel = 0; channel < 2; ++channel) {
                auto &frame = m_rfFrames[channel];
                if (!frame.metadata || frame.values.size() == frame.metadata->numberOfSamples) continue;
                frame = {};
                auto resend = channel == 0 ? m_rfSettings : m_rfSettings2;
                resend.command = DsoService::Command::ResendData;
                auto *service = channel == 0 ? m_dso : m_dso2;
                const auto role = channel + 1 == m_scopeTriggerSource
                    ? DsoService::SyncRole::Master : DsoService::SyncRole::Follower;
                service->setSynchronizedSettings(resend, m_scopeRfTriggerSample, role, m_rfGroup);
            }
            publishAutomationState();
            return;
        }
        failRfCapture(m_rfStage == RfStage::ArmingFollower
            ? QStringLiteral("Funktrigger: Empfänger hat das Scharfstellen nicht bestätigt")
            : QStringLiteral("Funktrigger: kein vollständiges Aufnahmepaar empfangen"));
    });
    m_meterWatchdog.setSingleShot(true);
    m_meterWatchdog.setInterval(5000);
    connect(&m_meterWatchdog, &QTimer::timeout, this, [this] {
        if (!m_connected || m_feature != QStringLiteral("meter")) return;
        if (!m_meterReady) {
            m_device->controller()->disconnectFromDevice();
            return;
        }
        m_meterValid = false;
        emit meterValueChanged();
        setStatus(QStringLiteral("Multimeterdaten fehlen – Kanal wird neu synchronisiert …"));
        m_meter->enableReadingNotifications();
        QTimer::singleShot(600, this, [this] {
            if (m_connected && m_feature == QStringLiteral("meter")) startMeter();
        });
    });
    m_meterWatchdog2.setSingleShot(true);
    m_meterWatchdog2.setInterval(5000);
    connect(&m_meterWatchdog2, &QTimer::timeout, this, [this] {
        if (!m_connected2 || m_feature != QStringLiteral("meter")) return;
        if (!m_meterReady2) {
            m_device2->controller()->disconnectFromDevice();
            return;
        }
        m_meterValid2 = false;
        emit meterValueChanged();
        setStatus(QStringLiteral("CH2 Multimeterdaten fehlen – Kanal wird neu synchronisiert …"));
        m_meter2->enableReadingNotifications();
        QTimer::singleShot(600, this, [this] {
            if (m_connected2 && m_feature == QStringLiteral("meter")) startMeter2();
        });
    });
    m_automationTimer.setInterval(250);
    connect(&m_automationTimer, &QTimer::timeout,
            this, &PokitController::processAutomationCommands);
    m_automationTimer.start();
    m_meterPoll.setInterval(1000);
    connect(&m_meterPoll, &QTimer::timeout, this, [this] {
        for (int channel = 0; channel < 2; ++channel) {
            if (!(channel ? m_connected2 : m_connected)) continue;
            if (m_feature != QStringLiteral("meter")) {
                if(!m_scopeRunning || !scopeChannelNotice(channel+1).isEmpty())refreshScopeSwitch(channel+1);
                continue;
            }
            refreshScopeSwitch(channel+1);
            auto *meter = channel ? m_meter2 : m_meter;
            auto &watchdog = channel ? m_meterWatchdog2 : m_meterWatchdog;
            if (meter && (channel ? m_meterReady2 : m_meterReady)
                && (!watchdog.isActive() || watchdog.remainingTime() < 3500)) {
                meter->readReadingCharacteristic();
            }
        }
    });
    m_meterPoll.start();
    publishAutomationState();
    refreshSavedCaptures();
    QTimer::singleShot(500, this, &PokitController::connectDevice);
}

QVariantList PokitController::devices() const
{
    QVariantMap known = m_savedDevices;
    const auto addCurrent = [&known](const QString &id, const QString &name, int channel, bool connected) {
        if (id.isEmpty()) return;
        QVariantMap entry = known.value(id).toMap();
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("channel"), channel);
        entry.insert(QStringLiteral("connected"), connected);
        known.insert(id, entry);
    };
    addCurrent(m_deviceId, m_deviceName, 1, m_connected);
    addCurrent(m_deviceId2, m_deviceName2, 2, m_connected2);
    QVariantList result;
    for (auto it = known.cbegin(); it != known.cend(); ++it) {
        QVariantMap entry = it.value().toMap();
        entry.insert(QStringLiteral("id"), it.key());
        entry.insert(QStringLiteral("saved"), m_savedDevices.contains(it.key()));
        result.append(entry);
    }
    return result;
}

QString PokitController::deviceColor() const
{
    return m_savedDevices.value(m_deviceId).toMap().value(QStringLiteral("color")).toString();
}

QString PokitController::deviceColor2() const
{
    return m_savedDevices.value(m_deviceId2).toMap().value(QStringLiteral("color")).toString();
}

void PokitController::saveDeviceColor(const QString &id, const QString &color)
{
    const QStringList palette{QStringLiteral("#ff6666"), QStringLiteral("#d3d3d3"),
                              QStringLiteral("#ffd629"), QStringLiteral("#55df89")};
    if (!palette.contains(color)) return;
    for (const QVariant &item : devices()) {
        const QVariantMap entry = item.toMap();
        if (entry.value(QStringLiteral("id")).toString() != id) continue;
        QVariantMap saved = m_savedDevices;
        saved.insert(id, QVariantMap{{QStringLiteral("name"), entry.value(QStringLiteral("name"))},
                                     {QStringLiteral("color"), color}});
        QSettings settings(QStringLiteral("PokitSuite"), QStringLiteral("PokitSuite"));
        settings.setValue(QStringLiteral("devices"), saved);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            setStatus(QStringLiteral("Gerätefarbe konnte nicht gespeichert werden"));
            return;
        }
        m_savedDevices = saved;
        emit devicesChanged();
        return;
    }
}

QVariantList PokitController::meterHistory() const
{
    QVariantList list; list.reserve(m_meterValues.size());
    for (double value : m_meterValues) list.append(value);
    return list;
}

QVariantList PokitController::meterHistory2() const
{
    QVariantList list; list.reserve(m_meterValues2.size());
    for (double value : m_meterValues2) list.append(value);
    return list;
}

QVariantList PokitController::scopeSamples() const
{
    QVariantList list; list.reserve(m_scopeValues.size());
    for (double value : m_scopeValues) list.append(value);
    return list;
}

QVariantList PokitController::scopeSamples2() const
{
    QVariantList list; list.reserve(m_scopeValues2.size());
    for (double value : m_scopeValues2) list.append(value);
    return list;
}

QVariantList PokitController::referenceSamples() const
{
    QVariantList list; list.reserve(m_referenceValues.size());
    for (double value : m_referenceValues) list.append(value);
    return list;
}

void PokitController::setStatus(const QString &status)
{
    if (m_status == status) return;
    m_status = status;
    qInfo().noquote() << "PokitSuite:" << status;
    emit statusChanged();
}

void PokitController::connectDevice()
{
    if (m_device && !m_connected) m_device->controller()->connectToDevice();
    if (m_device2 && !m_connected2) m_device2->controller()->connectToDevice();
    if (m_device2) return;
    if (m_discovery->isActive()) return;
    setStatus(m_device ? QStringLiteral("Suche nach zweitem Pokit …")
                       : QStringLiteral("Suche nach Pokit …"));
    m_discovery->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void PokitController::attachDevice(const QBluetoothDeviceInfo &info)
{
    const QString key = deviceKey(info);
    if (key.isEmpty() || key == m_deviceId || key == m_deviceId2) return;
    if (m_device) { attachSecondDevice(info); return; }
    m_deviceId = key;
    m_deviceName = info.name();
    emit deviceChanged();
    m_device = new PokitDevice(info, this);

    connect(m_device->controller(), &QLowEnergyController::connected, this, [this] {
        m_connected = true;
        emit connectedChanged();
        setStatus(QStringLiteral("Verbunden – Dienste werden initialisiert …"));
        QTimer::singleShot(400, this, [this] {
            if (!m_connected) return;
            if (m_feature == QStringLiteral("meter")) { ensureMeter(); startMeter(); }
            else { ensureDso(); startScope(); }
        });
    });
    connect(m_device->controller(), &QLowEnergyController::disconnected, this, [this] {
        m_meterWatchdog.stop();
        m_scopeWatchdog.stop();
        m_scopeRestartTimer.stop();
        m_scopeStableFrames = 0;
        m_scopeRestartTimer.setInterval(500);
        m_connected = false;
        m_rfNotificationsReady[0] = false;
        if (m_rfStage != RfStage::Idle) failRfCapture(QStringLiteral("Funktrigger: CH1 getrennt"));
        m_meterReady = false;
        m_meterStatusReady[0] = false;
        m_meterSwitchPosition[0].reset();m_switchQuery[0].reset();
        m_meterValid = m_meterOverrange = false;
        emit meterValueChanged();
        m_dsoReady = false;
        emit connectedChanged();
        emit scopeStateChanged();
        setStatus(QStringLiteral("Verbindung verloren – automatischer Neuaufbau …"));
        scheduleReconnect();
    });
    connect(m_device->controller(), &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error error) {
        qWarning().noquote() << "PokitSuite: Controllerfehler" << int(error)
                             << m_device->controller()->errorString();
        setStatus(QStringLiteral("Bluetooth-Fehler: %1").arg(m_device->controller()->errorString()));
        // The Windows backend can report a connection timeout while leaving
        // the controller in ConnectingState. Abort that stale attempt first.
        if (!m_connected) {
            m_device->controller()->disconnectFromDevice();
            scheduleReconnect();
        }
    });

    if (m_feature == QStringLiteral("meter")) ensureMeter(); else ensureDso();
    setStatus(QStringLiteral("Verbindung wird aufgebaut …"));
    m_device->controller()->connectToDevice();
}

void PokitController::attachSecondDevice(const QBluetoothDeviceInfo &info)
{
    if (m_device2) return;
    m_deviceId2 = deviceKey(info);
    m_deviceName2 = info.name().isEmpty() ? QStringLiteral("Pokit CH2") : info.name();
    emit deviceChanged();
    m_device2 = new PokitDevice(info, this);
    connect(m_device2->controller(), &QLowEnergyController::connected, this, [this] {
        m_connected2 = true;
        emit connectedChanged();
        setStatus(QStringLiteral("2 Pokits verbunden – Dienste werden initialisiert …"));
        QTimer::singleShot(450, this, [this] {
            if (!m_connected2) return;
            if (m_feature == QStringLiteral("meter")) { ensureMeter2(); startMeter2(); }
            else { ensureDso2(); startScope2(); }
        });
    });
    connect(m_device2->controller(), &QLowEnergyController::disconnected, this, [this] {
        m_meterWatchdog2.stop();
        m_scopeWatchdog2.stop();
        m_scopeRestartTimer2.stop();
        m_scopeStableFrames2 = 0;
        m_scopeRestartTimer2.setInterval(500);
        m_connected2 = false;
        m_rfNotificationsReady[1] = false;
        if (m_rfStage != RfStage::Idle) failRfCapture(QStringLiteral("Funktrigger: CH2 getrennt"));
        m_meterReady2 = false;
        m_meterStatusReady[1] = false;
        m_meterSwitchPosition[1].reset();m_switchQuery[1].reset();
        m_meterValid2 = m_meterOverrange2 = false;
        emit meterValueChanged();
        m_dsoReady2 = false;
        emit connectedChanged();
        emit scopeStateChanged();
        setStatus(QStringLiteral("CH2 Verbindung verloren – automatischer Neuaufbau …"));
        scheduleReconnect2();
    });
    connect(m_device2->controller(), &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error error) {
        qWarning().noquote() << "PokitSuite: CH2 Controllerfehler" << int(error)
                             << m_device2->controller()->errorString();
        if (!m_connected2) {
            m_device2->controller()->disconnectFromDevice();
            scheduleReconnect2();
        }
    });
    if (m_feature == QStringLiteral("meter")) ensureMeter2(); else ensureDso2();
    m_device2->controller()->connectToDevice();
}

void PokitController::ensureMeter()
{
    if (!m_device || m_meter) return;
    m_meter = m_device->multimeter();
    ensureMeterStatus(false);
    connect(m_meter, &AbstractPokitService::serviceDetailsDiscovered, this, [this] {
        m_meterReady = true;
        m_meter->enableReadingNotifications();
        QTimer::singleShot(700, this, [this] {
            if (m_feature == QStringLiteral("meter") && m_connected) startMeter();
        });
    });
    connect(m_meter, &AbstractPokitService::serviceErrorOccurred, this,
            [](QLowEnergyService::ServiceError error) { qWarning() << "Meter service error" << int(error); });
    connect(m_meter, &MultimeterService::readingRead, this,
            [this](const MultimeterService::Reading &reading) {
        if (m_feature != QStringLiteral("meter")) return;
        if (reading.mode != m_configuredMeterMode[0]) return;
        m_meterOverrange = reading.mode != MultimeterService::Mode::Continuity
            && std::abs(reading.value) >= 1e12;
        if (m_meterOverrange) {
            m_meterValid = false;
            m_meterWatchdog.start();
            emit meterValueChanged();
            publishAutomationState();
            return;
        }
        m_meterValid = reading.mode == m_configuredMeterMode[0]
            && reading.status != MultimeterService::MeterStatus::Error
            && (reading.mode == MultimeterService::Mode::Continuity || std::isfinite(reading.value));
        if (!m_meterValid) { emit meterValueChanged(); return; }
        m_meterWatchdog.start();
        m_meterValue = reading.mode == MultimeterService::Mode::Continuity
            ? double(reading.status == MultimeterService::MeterStatus::Continuity) : reading.value;
        m_meterTools.sample(1, m_meterValue);
        m_meterValues.append(m_meterValue);
        if (m_meterValues.size() > 7200) m_meterValues.remove(0, m_meterValues.size() - 7200);
        emit meterValueChanged(); emit meterHistoryChanged();
        setStatus(QStringLiteral("Live-Messung"));
        publishAutomationState();
    });
}

void PokitController::ensureMeter2()
{
    if (!m_device2 || m_meter2) return;
    m_meter2 = m_device2->multimeter();
    ensureMeterStatus(true);
    connect(m_meter2, &AbstractPokitService::serviceDetailsDiscovered, this, [this] {
        m_meterReady2 = true;
        m_meter2->enableReadingNotifications();
        QTimer::singleShot(700, this, [this] {
            if (m_feature == QStringLiteral("meter") && m_connected2) startMeter2();
        });
    });
    connect(m_meter2, &AbstractPokitService::serviceErrorOccurred, this,
            [](QLowEnergyService::ServiceError error) { qWarning() << "CH2 Meter service error" << int(error); });
    connect(m_meter2, &MultimeterService::readingRead, this,
            [this](const MultimeterService::Reading &reading) {
        if (m_feature != QStringLiteral("meter")) return;
        if (reading.mode != m_configuredMeterMode[1]) return;
        m_meterOverrange2 = reading.mode != MultimeterService::Mode::Continuity
            && std::abs(reading.value) >= 1e12;
        if (m_meterOverrange2) {
            m_meterValid2 = false;
            m_meterWatchdog2.start();
            emit meterValueChanged();
            publishAutomationState();
            return;
        }
        m_meterValid2 = reading.mode == m_configuredMeterMode[1]
            && reading.status != MultimeterService::MeterStatus::Error
            && (reading.mode == MultimeterService::Mode::Continuity || std::isfinite(reading.value));
        if (!m_meterValid2) { emit meterValueChanged(); return; }
        m_meterWatchdog2.start();
        m_meterValue2 = reading.mode == MultimeterService::Mode::Continuity
            ? double(reading.status == MultimeterService::MeterStatus::Continuity) : reading.value;
        m_meterTools.sample(2, m_meterValue2);
        m_meterValues2.append(m_meterValue2);
        if (m_meterValues2.size() > 7200) m_meterValues2.remove(0, m_meterValues2.size() - 7200);
        emit meterValueChanged(); emit meterHistoryChanged();
        setStatus(QStringLiteral("Live-Messung · 2 Kanäle"));
        publishAutomationState();
    });
}

static QString switchLabel(std::optional<StatusService::SwitchPosition> position)
{
    using Position = StatusService::SwitchPosition;
    if (position == Position::Voltage) return QStringLiteral("Schalter: Spannung");
    if (position == Position::MultiMode) return QStringLiteral("Schalter: Kombi / mA");
    if (position == Position::HighCurrent) return QStringLiteral("Schalter: hoher Strom");
    return QStringLiteral("Schalter: unbekannt");
}

QString PokitController::meterSwitch() const { return switchLabel(m_meterSwitchPosition[0]); }
QString PokitController::meterSwitch2() const { return switchLabel(m_meterSwitchPosition[1]); }

void PokitController::ensureMeterStatus(bool second)
{
    auto *status = (second ? m_device2 : m_device)->status();
    if (m_meterStatus[second] == status) return;
    m_meterStatus[second] = status;
    connect(status, &AbstractPokitService::serviceDetailsDiscovered, this, [this, second, status] {
        m_meterStatusReady[second] = true;
        auto &query=m_switchQuery[second];query.reset();
        auto *service=status->service();
        if(!service)return;
        const auto characteristic=service->characteristic(StatusService::CharacteristicUuids::status);
        const auto cccd=characteristic.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        query.canNotify=characteristic.properties().testFlag(QLowEnergyCharacteristic::Notify)&&cccd.isValid();
        qInfo()<<"Switch CH"<<second+1<<"properties"<<int(characteristic.properties())<<"notifications supported"<<query.canNotify;
        connect(service,&QLowEnergyService::descriptorWritten,this,[this,second,status,service,cccd](const QLowEnergyDescriptor &descriptor,const QByteArray &value){
            if(status->service()!=service||descriptor!=cccd)return;
            auto &q=m_switchQuery[second];q.subscribing=false;q.notifications=(value==QByteArray::fromHex("0100"));
            if(q.notifications&&q.confirmedAt>=0)q.nextAttempt=m_switchClock.elapsed()+q.interval();
            qInfo()<<"Switch CH"<<second+1<<"notification subscription"<<q.notifications;
            emit scopeStateChanged();
            QTimer::singleShot(50,this,[this,second]{refreshScopeSwitch(second+1);resumeAfterSwitch(second+1);});
        });
        const auto trace=[this,second](const QLowEnergyCharacteristic &c,const QByteArray &value,bool notification){
            if(c.uuid()!=StatusService::CharacteristicUuids::status)return;
            auto &q=m_switchQuery[second];if(notification)++q.updates;else ++q.reads;
            qInfo()<<"Switch CH"<<second+1<<(notification?"notify":"read")<<value.toHex();
        };
        connect(service,&QLowEnergyService::characteristicRead,this,[trace](const QLowEnergyCharacteristic &c,const QByteArray &v){trace(c,v,false);});
        connect(service,&QLowEnergyService::characteristicChanged,this,[trace](const QLowEnergyCharacteristic &c,const QByteArray &v){trace(c,v,true);});
        QTimer::singleShot(1200,this,[this,second]{if(second?m_connected2:m_connected)refreshScopeSwitch(second+1);});
    });
    connect(status,&AbstractPokitService::serviceErrorOccurred,this,[this,second,status](QLowEnergyService::ServiceError error){
        auto &q=m_switchQuery[second];
        qWarning()<<"Switch CH"<<second+1<<"service error"<<int(error);
        if(q.subscribing){q.subscribing=false;q.notifications=false;q.error=QStringLiteral("Status-Benachrichtigung nicht bestätigt; Leseabfrage wird verwendet");resumeAfterSwitch(second+1);}
        else if(q.pending)failSwitchQuery(second+1,q.token,QStringLiteral("Bluetooth-Statusfehler %1").arg(int(error)));
        if(!status->service()||status->service()->state()==QLowEnergyService::InvalidService){
            m_meterStatusReady[second]=false;q.uncertain=true;
            if(second?m_connected2:m_connected)(second?m_device2:m_device)->controller()->disconnectFromDevice();
        }
        emit scopeStateChanged();
    });
    connect(status, &StatusService::deviceStatusUpdate, this, [this, second](const StatusService::Status &reading,bool notification) {
        if (!(second ? m_connected2 : m_connected) || !reading.switchPosition || quint8(*reading.switchPosition)>2) return;
        auto &query=m_switchQuery[second];
        if(!notification&&query.readSuperseded()) {
            const bool waiting=query.pending;query.pending=false;
            if(waiting)resumeAfterSwitch(second+1);
            emit scopeStateChanged();return;
        }
        const bool waiting=query.confirm(m_switchClock.elapsed(),notification);
        if(reading.switchPosition==m_meterSwitchPosition[second]) {
            if(waiting)resumeAfterSwitch(second+1);
            emit scopeStateChanged();publishAutomationState();return;
        }
        m_meterSwitchPosition[second] = reading.switchPosition;
        qInfo()<<"Switch CH"<<second+1<<"confirmed position"<<int(*reading.switchPosition);
        if (m_feature == QStringLiteral("meter")) configureMeter(second);
        else scopeSwitchChanged(second ? 2 : 1);
        emit meterValueChanged();
    });
}

void PokitController::ensureDso()
{
    if (!m_device || m_dso) return;
    ensureMeterStatus(false);
    m_dso = m_device->dso();
    connect(m_dso, &AbstractPokitService::serviceDetailsDiscovered, this, [this] {
        m_dsoReady = true;
        m_dso->enableMetadataNotifications();
        // Serialize the two CCCD writes; Qt/Windows can otherwise drop the
        // second write while the first one is still pending.
        QTimer::singleShot(400, this, [this] {
            if (!m_connected) return;
            m_dso->enableReadingNotifications();
            QTimer::singleShot(700, this, [this] {
                m_rfNotificationsReady[0] = m_connected && m_dsoReady;
                emit scopeStateChanged();
                publishAutomationState();
                if (m_feature == QStringLiteral("scope") && m_connected) startScope();
            });
        });
    });
    connect(m_dso, &AbstractPokitService::serviceErrorOccurred, this,
            [](QLowEnergyService::ServiceError error) { qWarning() << "DSO service error" << int(error); });
    connect(m_dso, &DsoService::synchronizedSettingsWritten, this,
            [this](DsoService::SyncRole role, quint8 group) { onRfSettingsWritten(1, role, group); });
    connect(m_dso, &DsoService::metadataRead, this, [this](const DsoService::Metadata &meta) {
        if (m_feature != QStringLiteral("scope")) return;
        if (m_scopeTriggerSource) { onRfMetadata(1, meta); return; }
        if (meta.mode != scopeSettings(1).mode || !scopeChannelNotice(1).isEmpty() || meta.samplingWindow != m_expectedScopeWindowUs ||
            meta.numberOfSamples != m_expectedScopeSamples) {
            return;
        }
        m_scopeAccepting = true;
        m_scopeMetadata = meta;
        m_scopePendingValues.clear();
        m_scopePendingValues.reserve(meta.numberOfSamples);
        m_scopeWatchdog.start();
        emit scopeStateChanged();
    });
    connect(m_dso, &DsoService::samplesRead, this, [this](const DsoService::Samples &samples) {
        if (m_scopeTriggerSource) { onRfSamples(1, samples); return; }
        if (!m_scopeAccepting || m_feature != QStringLiteral("scope")) return;
        m_scopeWatchdog.start();
        for (qint16 sample : samples)
            m_scopePendingValues.append(sample * m_scopeMetadata.scale);
        if (m_feature == QStringLiteral("scope") && m_scopeMetadata.numberOfSamples > 0 &&
            m_scopePendingValues.size() >= m_scopeMetadata.numberOfSamples) {
            // Publish complete acquisitions atomically. Drawing partial BLE
            // chunks across the full plot width distorts the time axis.
            m_scopePendingValues.resize(m_scopeMetadata.numberOfSamples);
            m_scopeValues = m_scopePendingValues;
            m_scopeAccepting = false;
            m_rfNotificationsReady[0] = true;
            updateScopeMetrics();
            emit scopeSamplesChanged();
            publishAutomationState();
            if (++m_scopeStableFrames >= 5)
                m_scopeRestartTimer.setInterval(150);
            setStatus(QStringLiteral("Scope · %1 Samples · %2 kS/s")
                      .arg(m_scopeValues.size())
                      .arg(m_scopeMetadata.samplingRate / 1000.0, 0, 'f', 1));
            if (m_scopeSingle) {
                m_scopeWatchdog.stop();
                m_scopeSingle = false;
                m_scopeRunning = false;
                emit scopeStateChanged();
                setStatus(QStringLiteral("Scope · Einzelaufnahme abgeschlossen"));
                return;
            }
            // Give firmware 2.x time to finish the notification transaction
            // before writing the next free-running acquisition settings.
            if (m_feature == QStringLiteral("scope") && m_connected && m_scopeRunning)
                m_scopeRestartTimer.start();
        }
    });
}

void PokitController::ensureDso2()
{
    if (!m_device2 || m_dso2) return;
    ensureMeterStatus(true);
    m_dso2 = m_device2->dso();
    connect(m_dso2, &AbstractPokitService::serviceDetailsDiscovered, this, [this] {
        m_dsoReady2 = true;
        m_dso2->enableMetadataNotifications();
        QTimer::singleShot(400, this, [this] {
            if (!m_connected2) return;
            m_dso2->enableReadingNotifications();
            QTimer::singleShot(700, this, [this] {
                m_rfNotificationsReady[1] = m_connected2 && m_dsoReady2;
                emit scopeStateChanged();
                publishAutomationState();
                if (m_feature == QStringLiteral("scope") && m_connected2) startScope2();
            });
        });
    });
    connect(m_dso2, &AbstractPokitService::serviceErrorOccurred, this,
            [](QLowEnergyService::ServiceError error) { qWarning() << "CH2 DSO service error" << int(error); });
    connect(m_dso2, &DsoService::synchronizedSettingsWritten, this,
            [this](DsoService::SyncRole role, quint8 group) { onRfSettingsWritten(2, role, group); });
    connect(m_dso2, &DsoService::metadataRead, this, [this](const DsoService::Metadata &meta) {
        if (m_feature != QStringLiteral("scope")) return;
        if (m_scopeTriggerSource) { onRfMetadata(2, meta); return; }
        if (meta.mode != scopeSettings(2).mode || !scopeChannelNotice(2).isEmpty() || meta.samplingWindow != m_expectedScopeWindowUs2 ||
            meta.numberOfSamples != m_expectedScopeSamples2) return;
        m_scopeAccepting2 = true;
        m_scopeMetadata2 = meta;
        m_scopePendingValues2.clear();
        m_scopePendingValues2.reserve(meta.numberOfSamples);
        m_scopeWatchdog2.start();
        emit scopeStateChanged();
    });
    connect(m_dso2, &DsoService::samplesRead, this, [this](const DsoService::Samples &samples) {
        if (m_scopeTriggerSource) { onRfSamples(2, samples); return; }
        if (!m_scopeAccepting2 || m_feature != QStringLiteral("scope")) return;
        m_scopeWatchdog2.start();
        for (qint16 sample : samples) m_scopePendingValues2.append(sample * m_scopeMetadata2.scale);
        if (m_scopeMetadata2.numberOfSamples > 0 &&
            m_scopePendingValues2.size() >= m_scopeMetadata2.numberOfSamples) {
            m_scopePendingValues2.resize(m_scopeMetadata2.numberOfSamples);
            m_scopeValues2 = m_scopePendingValues2;
            m_scopeAccepting2 = false;
            m_rfNotificationsReady[1] = true;
            updateScopeMetrics2();
            emit scopeSamplesChanged();
            publishAutomationState();
            if (++m_scopeStableFrames2 >= 5) m_scopeRestartTimer2.setInterval(150);
            if (m_feature == QStringLiteral("scope") && m_connected2 && m_scopeRunning)
                m_scopeRestartTimer2.start();
        }
    });
}

void PokitController::updateScopeMetrics()
{
    if (m_scopeValues.isEmpty()) return;
    const auto [minimum, maximum] = std::minmax_element(m_scopeValues.cbegin(), m_scopeValues.cend());
    m_scopeMinimum = *minimum;
    m_scopeMaximum = *maximum;
    m_scopeVpp = m_scopeMaximum - m_scopeMinimum;
    double sum = 0.0;
    double squareSum = 0.0;
    for (double value : m_scopeValues) {
        sum += value;
        squareSum += value * value;
    }
    m_scopeMean = sum / m_scopeValues.size();
    m_scopeRms = std::sqrt(squareSum / m_scopeValues.size());

    const double threshold = (m_scopeMinimum + m_scopeMaximum) * 0.5;
    QVector<int> risingEdges;
    int highSamples = 0;
    for (int index = 1; index < m_scopeValues.size(); ++index) {
        if (m_scopeValues[index] >= threshold) ++highSamples;
        if (m_scopeValues[index - 1] < threshold && m_scopeValues[index] >= threshold)
            risingEdges.append(index);
    }
    m_scopeDutyCycle = 100.0 * highSamples / qMax(1, m_scopeValues.size() - 1);
    m_scopeCycles = qMax(0, risingEdges.size() - 1);
    m_scopeFrequency = 0.0;
    m_scopePeriod = 0.0;
    if (risingEdges.size() >= 2 && m_scopeMetadata.samplingRate > 0) {
        const double averageSamples = double(risingEdges.last() - risingEdges.first()) /
                                      double(risingEdges.size() - 1);
        m_scopeFrequency = m_scopeMetadata.samplingRate / averageSamples;
        m_scopePeriod = 1.0 / m_scopeFrequency;
    }
    emit scopeMetricsChanged();
}

void PokitController::updateScopeMetrics2()
{
    if (m_scopeValues2.isEmpty()) return;
    const auto [minimum, maximum] = std::minmax_element(m_scopeValues2.cbegin(), m_scopeValues2.cend());
    m_scopeMinimum2 = *minimum;
    m_scopeMaximum2 = *maximum;
    m_scopeVpp2 = m_scopeMaximum2 - m_scopeMinimum2;
    double sum = 0.0, squareSum = 0.0;
    for (double value : m_scopeValues2) { sum += value; squareSum += value * value; }
    m_scopeMean2 = sum / m_scopeValues2.size();
    m_scopeRms2 = std::sqrt(squareSum / m_scopeValues2.size());
    const double threshold = (m_scopeMinimum2 + m_scopeMaximum2) * 0.5;
    QVector<int> risingEdges;
    int highSamples = 0;
    for (int index = 1; index < m_scopeValues2.size(); ++index) {
        if (m_scopeValues2[index] >= threshold) ++highSamples;
        if (m_scopeValues2[index - 1] < threshold && m_scopeValues2[index] >= threshold)
            risingEdges.append(index);
    }
    m_scopeDutyCycle2 = 100.0 * highSamples / qMax(1, m_scopeValues2.size() - 1);
    m_scopeCycles2 = qMax(0, risingEdges.size() - 1);
    m_scopeFrequency2 = 0.0;
    m_scopePeriod2 = 0.0;
    if (risingEdges.size() >= 2 && m_scopeMetadata2.samplingRate > 0) {
        const double averageSamples = double(risingEdges.last() - risingEdges.first()) /
                                      double(risingEdges.size() - 1);
        m_scopeFrequency2 = m_scopeMetadata2.samplingRate / averageSamples;
        m_scopePeriod2 = 1.0 / m_scopeFrequency2;
    }
    emit scopeMetricsChanged();
}

QString PokitController::captureStoragePath() const
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(documents).filePath(QStringLiteral("PokitSuite/Captures"));
}

void PokitController::refreshSavedCaptures()
{
    QDir directory(captureStoragePath());
    directory.mkpath(QStringLiteral("."));
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("PokitScope_*.json")}, QDir::Files, QDir::Time);
    QVariantList captures;
    for (const QFileInfo &info : files) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) continue;
        const QJsonObject object = document.object();
        const QJsonObject metrics = object.value(QStringLiteral("metrics")).toObject();
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), object.value(QStringLiteral("display_name")).toString(info.baseName()));
        entry.insert(QStringLiteral("path"), info.filePath());
        entry.insert(QStringLiteral("pdfPath"), info.dir().filePath(info.completeBaseName() + QStringLiteral(".pdf")));
        entry.insert(QStringLiteral("sampleCount"), ScopeExport::waveform(object).size());
        entry.insert(QStringLiteral("unit"), object.value(QStringLiteral("unit")).toString(QStringLiteral("V")));
        entry.insert(QStringLiteral("vpp"), metrics.contains(QStringLiteral("vpp"))?metrics.value(QStringLiteral("vpp")).toDouble():metrics.value(QStringLiteral("vpp_v")).toDouble());
        entry.insert(QStringLiteral("frequency"), metrics.value(QStringLiteral("frequency_hz")).toDouble());
        captures.append(entry);
    }
    m_savedCaptures = captures;
    emit savedCapturesChanged();
}

bool PokitController::writeCapturePdf(const QString &path, const QJsonObject &capture) const
{
    return ScopeExport::writePdf(path, capture);
}

QString PokitController::saveScopeCapture()
{
    QJsonArray channels;
    for (int index = 0; index < 2; ++index) {
        const auto &values = index ? m_scopeValues2 : m_scopeValues;
        const auto &metadata = index ? m_scopeMetadata2 : m_scopeMetadata;
        if (values.size() < 2 || !metadata.samplingRate) continue;
        const auto &config = m_scopeChannels.at(index + 1);
        QJsonObject metrics{
            {QStringLiteral("minimum_v"), index ? m_scopeMinimum2 : m_scopeMinimum},
            {QStringLiteral("maximum_v"), index ? m_scopeMaximum2 : m_scopeMaximum},
            {QStringLiteral("vpp_v"), index ? m_scopeVpp2 : m_scopeVpp},
            {QStringLiteral("mean_v"), index ? m_scopeMean2 : m_scopeMean},
            {QStringLiteral("rms_v"), index ? m_scopeRms2 : m_scopeRms},
            {QStringLiteral("frequency_hz"), index ? m_scopeFrequency2 : m_scopeFrequency},
            {QStringLiteral("period_s"), index ? m_scopePeriod2 : m_scopePeriod},
            {QStringLiteral("cycles"), index ? m_scopeCycles2 : m_scopeCycles},
            {QStringLiteral("duty_cycle_percent"), index ? m_scopeDutyCycle2 : m_scopeDutyCycle}
        };
        QJsonObject settings{
            {QStringLiteral("sample_rate_hz"), double(metadata.samplingRate)},
            {QStringLiteral("sampling_window_us"), double(metadata.samplingWindow)},
            {QStringLiteral("time_per_div_us"), m_scopeTimePerDivUs},
            {QStringLiteral("volts_per_div"), config.voltsPerDiv},
            {QStringLiteral("vertical_position_v"), config.offset},
            {QStringLiteral("coupling"), metadata.mode==DsoService::Mode::AcCurrent||metadata.mode==DsoService::Mode::AcVoltage?QStringLiteral("AC"):QStringLiteral("DC")},
            {QStringLiteral("trigger_mode"), m_triggerMode},
            {QStringLiteral("trigger_level_v"), m_scopeRfPair?(index?m_rfSettings2.triggerLevel:m_rfSettings.triggerLevel):scopeSettings(index+1).triggerLevel},
            {QStringLiteral("trigger_sample"), m_scopeRfPair ? QJsonValue(metadata.triggerSample.value_or(0)) : QJsonValue()}
        };
        QJsonArray waveform;
        for (double value : values) waveform.append(value);
        channels.append(ScopeExport::normalize(QJsonObject{
            {QStringLiteral("channel"), index + 1},
            {QStringLiteral("device"), index ? m_deviceName2 : m_deviceName},
            {QStringLiteral("device_id"), index ? m_deviceId2 : m_deviceId},
            {QStringLiteral("color"), index ? deviceColor2() : deviceColor()},
            {QStringLiteral("visible"), config.visible},
            {QStringLiteral("connected"), index ? m_connected2 : m_connected},
            {QStringLiteral("metrics"), metrics}, {QStringLiteral("settings"), settings},
            {QStringLiteral("waveform_v"), waveform}
        },config.current));
    }
    if (channels.isEmpty()) {
        setStatus(QStringLiteral("Keine vollständige Scope-Aufnahme zum Speichern vorhanden"));
        return {};
    }
    QDir directory(captureStoragePath());
    if (!directory.mkpath(QStringLiteral("."))) {
        setStatus(QStringLiteral("Capture-Ordner konnte nicht erstellt werden"));
        return {};
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QString base = QStringLiteral("PokitScope_%1").arg(now.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    // Keep legacy top-level fields for existing reference-curve readers.
    QJsonObject capture = channels.first().toObject();
    capture.insert(QStringLiteral("schema"), QStringLiteral("pokitsuite.scope-capture.v1"));
    capture.insert(QStringLiteral("display_name"), now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    capture.insert(QStringLiteral("timestamp_utc"), now.toUTC().toString(Qt::ISODateWithMs));
    capture.insert(QStringLiteral("channels"), channels);
    capture.insert(QStringLiteral("radio_paired"), m_scopeRfPair);
    capture.insert(QStringLiteral("trigger_source"), m_scopeTriggerSource);
    capture.insert(QStringLiteral("trigger_unit"), m_scopeChannels.at(m_scopeTriggerSource==2?2:1).unit());
    capture.insert(QStringLiteral("trigger_source_level"), m_scopeTriggerSource==2?m_rfSettings2.triggerLevel:m_rfSettings.triggerLevel);
    capture.insert(QStringLiteral("trigger_fraction"), m_scopeRfPair ? scopeRfTriggerFraction() : 0.0);
    capture.insert(QStringLiteral("view_window_us"), m_scopeRfPair ? int(m_rfSettings.samplingWindow) : m_scopeTimePerDivUs * 10);
    const QString jsonPath = directory.filePath(base + QStringLiteral(".json"));
    QSaveFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly) ||
        jsonFile.write(QJsonDocument(capture).toJson(QJsonDocument::Indented)) < 0 || !jsonFile.commit()) {
        setStatus(QStringLiteral("Messdatei konnte nicht gespeichert werden"));
        return {};
    }
    if (!ScopeExport::writeCsv(directory.filePath(base + QStringLiteral(".csv")), capture)) {
        setStatus(QStringLiteral("Messdatei gespeichert, CSV-Erstellung fehlgeschlagen"));
        refreshSavedCaptures();
        return {};
    }
    const QString pdfPath = directory.filePath(base + QStringLiteral(".pdf"));
    if (!writeCapturePdf(pdfPath, capture)) {
        setStatus(QStringLiteral("Messwerte gespeichert, PDF-Erstellung fehlgeschlagen"));
        refreshSavedCaptures();
        return {};
    }
    refreshSavedCaptures();
    setStatus(QStringLiteral("Scope-Bericht mit %1 Kanälen als PDF und Wertetabelle gespeichert").arg(channels.size()));
    return pdfPath;
}

void PokitController::loadScopeCapture(const QString &jsonPath)
{
    const QFileInfo info(jsonPath);
    const QDir storage(captureStoragePath());
    const QString relativePath = storage.relativeFilePath(info.absoluteFilePath());
    if (!info.exists() || info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0 ||
        relativePath == QStringLiteral("..") || relativePath.startsWith(QStringLiteral("../")) ||
        QDir::isAbsolutePath(relativePath)) {
        setStatus(QStringLiteral("Ungültige gespeicherte Aufnahme"));
        return;
    }
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("Gespeicherte Aufnahme konnte nicht geöffnet werden"));
        return;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray waveform = ScopeExport::waveform(object);
    QVector<double> values; values.reserve(waveform.size());
    for (const QJsonValue &value : waveform) {
        if (!value.isDouble() || !std::isfinite(value.toDouble())) {
            setStatus(QStringLiteral("Gespeicherte Aufnahme enthält ungültige Messwerte"));
            return;
        }
        values.append(value.toDouble());
    }
    if (values.size() < 2) {
        setStatus(QStringLiteral("Gespeicherte Aufnahme enthält keine Kurve"));
        return;
    }
    const auto settings = object.value(QStringLiteral("settings")).toObject();
    double rate = settings.value(QStringLiteral("sample_rate_hz")).toDouble();
    const double window = settings.value(QStringLiteral("sampling_window_us")).toDouble();
    if (!(rate > 0) && window > 0) rate = values.size() * 1e6 / window;
    if (!(rate > 0) || !std::isfinite(rate)) {
        setStatus(QStringLiteral("Gespeicherte Aufnahme enthält keine gültige Zeitbasis"));
        return;
    }
    const auto trigger = settings.value(QStringLiteral("trigger_sample"));
    m_referenceHasTrigger = trigger.isDouble() && trigger.toDouble() >= 0
        && trigger.toDouble() <= values.size();
    m_referenceTriggerSample = m_referenceHasTrigger ? trigger.toInt() : 0;
    m_referenceSampleRate = rate;
    m_referenceUnit = object.value(QStringLiteral("unit")).toString(QStringLiteral("V"));
    m_referenceValues = values;
    m_referenceName = object.value(QStringLiteral("display_name")).toString(info.baseName());
    emit referenceChanged();
    setStatus(QStringLiteral("Vergleichskurve geladen: %1").arg(m_referenceName));
}

void PokitController::clearScopeReference()
{
    m_referenceValues.clear();
    m_referenceName.clear();
    m_referenceSampleRate = 0;
    m_referenceTriggerSample = 0;
    m_referenceHasTrigger = false;
    emit referenceChanged();
}

void PokitController::openCaptureFolder()
{
    QDir directory(captureStoragePath());
    directory.mkpath(QStringLiteral("."));
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath()));
}

void PokitController::publishAutomationState()
{
    const QDir root=automationRoot();
    root.mkpath(QStringLiteral("data"));
    QJsonObject metrics{
        {QStringLiteral("minimum_v"), m_scopeMinimum},
        {QStringLiteral("maximum_v"), m_scopeMaximum},
        {QStringLiteral("vpp_v"), m_scopeVpp},
        {QStringLiteral("mean_v"), m_scopeMean},
        {QStringLiteral("rms_v"), m_scopeRms},
        {QStringLiteral("frequency_hz"), m_scopeFrequency},
        {QStringLiteral("period_s"), m_scopePeriod},
        {QStringLiteral("cycles"), m_scopeCycles},
        {QStringLiteral("duty_cycle_percent"), m_scopeDutyCycle}
    };
    QJsonArray waveform;
    for (double value : m_scopeValues) waveform.append(value);
    QJsonObject metrics2{
        {QStringLiteral("minimum_v"), m_scopeMinimum2},
        {QStringLiteral("maximum_v"), m_scopeMaximum2},
        {QStringLiteral("vpp_v"), m_scopeVpp2},
        {QStringLiteral("rms_v"), m_scopeRms2},
        {QStringLiteral("frequency_hz"), m_scopeFrequency2}
    };
    QJsonArray waveform2;
    for (double value : m_scopeValues2) waveform2.append(value);
    QJsonObject scope{
        {QStringLiteral("device_scale"), double(m_scopeMetadata.scale)},
        {QStringLiteral("device_range"), int(m_scopeMetadata.range)},
        {QStringLiteral("sample_rate_hz"), double(m_scopeMetadata.samplingRate)},
        {QStringLiteral("sampling_window_us"), double(m_scopeMetadata.samplingWindow)},
        {QStringLiteral("samples"), m_scopeValues.size()},
        {QStringLiteral("time_per_div_us"), m_scopeTimePerDivUs},
        {QStringLiteral("volts_per_div"), scopeVoltsPerDiv()},
        {QStringLiteral("coupling"), m_scopeCoupling},
        {QStringLiteral("trigger_mode"), m_triggerMode},
        {QStringLiteral("trigger_level_v"), m_triggerLevel},
        {QStringLiteral("reference_name"), m_referenceName},
        {QStringLiteral("reference_samples"), m_referenceValues.size()},
        {QStringLiteral("metrics"), metrics},
        {QStringLiteral("waveform_v"), waveform}
    };
    QJsonObject scope2{
        {QStringLiteral("device_scale"), double(m_scopeMetadata2.scale)},
        {QStringLiteral("device_range"), int(m_scopeMetadata2.range)},
        {QStringLiteral("sample_rate_hz"), double(m_scopeMetadata2.samplingRate)},
        {QStringLiteral("sampling_window_us"), double(m_scopeMetadata2.samplingWindow)},
        {QStringLiteral("samples"), m_scopeValues2.size()},
        {QStringLiteral("time_per_div_us"), m_scopeTimePerDivUs},
        {QStringLiteral("volts_per_div"), m_scopeChannels.at(2).voltsPerDiv},
        {QStringLiteral("coupling"), m_scopeCoupling},
        {QStringLiteral("trigger_mode"), m_triggerMode},
        {QStringLiteral("trigger_level_v"), m_triggerLevel},
        {QStringLiteral("metrics"), metrics2},
        {QStringLiteral("waveform_v"), waveform2}
    };
    scope.insert(QStringLiteral("trigger_level_v"),scopeSettings(1).triggerLevel);
    scope2.insert(QStringLiteral("trigger_level_v"),scopeSettings(2).triggerLevel);
    scope = ScopeExport::normalize(scope,m_scopeChannels.at(1).current);
    scope2 = ScopeExport::normalize(scope2,m_scopeChannels.at(2).current);
    QJsonObject state{
        {QStringLiteral("scope_channels"), QJsonArray::fromVariantList(scopeChannels())},
        {QStringLiteral("schema"), QStringLiteral("pokitsuite.telemetry.v1")},
        {QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("connected"), m_connected},
        {QStringLiteral("device"), m_deviceName},
        {QStringLiteral("connected2"), m_connected2},
        {QStringLiteral("device2"), m_deviceName2},
        {QStringLiteral("connected_device_count"), (m_connected ? 1 : 0) + (m_connected2 ? 1 : 0)},
        {QStringLiteral("status"), m_status},
        {QStringLiteral("active_feature"), m_feature},
        {QStringLiteral("scope_running"),m_scopeRunning},
        {QStringLiteral("multimeter"), QJsonObject{
            {QStringLiteral("value"), m_meterValue}, {QStringLiteral("unit"), m_meterUnit},
            {QStringLiteral("valid"), m_connected && m_meterValid},
            {QStringLiteral("overrange"), m_connected && m_meterOverrange},
            {QStringLiteral("switch_position"), meterSwitch()}}},
        {QStringLiteral("multimeter2"), QJsonObject{
            {QStringLiteral("value"), m_meterValue2}, {QStringLiteral("unit"), m_meterUnit2},
            {QStringLiteral("valid"), m_connected2 && m_meterValid2},
            {QStringLiteral("overrange"), m_connected2 && m_meterOverrange2},
            {QStringLiteral("switch_position"), meterSwitch2()}}},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("scope2"), scope2},
        {QStringLiteral("scope_sync"), QJsonObject{
            {QStringLiteral("trigger_source"), m_scopeTriggerSource},
            {QStringLiteral("ready"), scopeRfReady()},
            {QStringLiteral("running"), m_scopeTriggerSource && m_scopeRunning},
            {QStringLiteral("paired"), m_scopeRfPair},
            {QStringLiteral("capture_sequence"), m_scopeRfPair
                ? QJsonValue(int(*m_scopeMetadata.captureSequence)) : QJsonValue()},
            {QStringLiteral("trigger_sample_ch1"), m_scopeRfPair
                ? QJsonValue(scopeRfTriggerSample()) : QJsonValue()},
            {QStringLiteral("trigger_sample_ch2"), m_scopeRfPair
                ? QJsonValue(scopeRfTriggerSample2()) : QJsonValue()}
        }},
        {QStringLiteral("channels"), QJsonArray{scope, scope2}}
    };
    QSaveFile file(root.filePath(QStringLiteral("data/pokit_live.json")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void PokitController::processAutomationCommands()
{
    const QDir root=automationRoot();
    const QString commandPath = root.filePath(QStringLiteral("data/pokit_command.json"));
    QFile file(commandPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    QFile::remove(commandPath);
    if (!document.isObject()) return;
    const QJsonObject command = document.object();
    const QString action = command.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("show_scope")) showScope();
    else if (action == QStringLiteral("show_multimeter")) showMultimeter();
    else if (action == QStringLiteral("scope_run")) setScopeRunning(true);
    else if (action == QStringLiteral("scope_stop")) setScopeRunning(false);
    else if (action == QStringLiteral("scope_single")) scopeSingle();
    else if (action == QStringLiteral("scope_trigger_source"))
        setScopeTriggerSource(command.value(QStringLiteral("channel")).toInt(-1));
    else if (action == QStringLiteral("scope_channel")) {
        const int channel = command.value(QStringLiteral("channel")).toInt();
        if (channel >= 1 && channel <= 4) {
            if(command.contains(QStringLiteral("current")))setScopeChannelCurrent(channel,command.value(QStringLiteral("current")).toBool());
            const auto previous = m_scopeChannels.at(channel);
            if (command.contains(QStringLiteral("visible"))) setScopeChannelVisible(channel,command.value(QStringLiteral("visible")).toBool());
            setScopeChannelScale(channel,command.value(QStringLiteral("volts_per_div")).toDouble(previous.voltsPerDiv),
                command.value(QStringLiteral("offset_v")).toDouble(previous.offset));
        }
    }
    else if (action == QStringLiteral("save_scope_capture")) saveScopeCapture();
    else if (action == QStringLiteral("load_scope_reference"))
        loadScopeCapture(command.value(QStringLiteral("json_path")).toString());
    else if (action == QStringLiteral("clear_scope_reference")) clearScopeReference();
    else if (action == QStringLiteral("scope_config")) {
        setScopeConfig(command.value(QStringLiteral("coupling")).toString(QStringLiteral("DC")),
                       command.value(QStringLiteral("volts_per_div")).toDouble(scopeVoltsPerDiv()),
                       command.value(QStringLiteral("time_per_div_us")).toInt(m_scopeTimePerDivUs));
        setScopeTrigger(command.value(QStringLiteral("trigger_mode")).toString(m_triggerMode),
                        command.value(QStringLiteral("trigger_level_v")).toDouble(m_triggerLevel));
    }
    publishAutomationState();
}

void PokitController::scheduleReconnect()
{
    if (!m_reconnectTimer.isActive()) m_reconnectTimer.start();
}

void PokitController::scheduleReconnect2()
{
    if (!m_reconnectTimer2.isActive()) m_reconnectTimer2.start();
}

void PokitController::showMultimeter()
{
    if (m_feature == QStringLiteral("meter")) return;
    m_rfContinuous = false;
    m_rfRestart.stop();
    if (m_rfStage != RfStage::Idle) {
        stopRfCapture();
        m_scopeRunning = false;
    }
    clearRfPair();
    m_scopeAccepting = false;
    m_scopeAccepting2 = false;
    m_scopeWatchdog.stop();
    m_scopeWatchdog2.stop();
    m_scopeRestartTimer.stop();
    m_scopeRestartTimer2.stop();
    // Keep CCCD subscriptions alive. Repeated disable/enable descriptor writes
    // are unreliable with the Windows BLE backend and Pokit firmware 2.x.
    // Late DSO packets are discarded by m_scopeAccepting instead.
    m_feature = QStringLiteral("meter"); emit activeFeatureChanged();
    activateCurrentFeature();
}

void PokitController::showScope()
{
    if (m_feature == QStringLiteral("scope")) return;
    if (m_meter) {
        m_meter->setSettings({MultimeterService::Mode::Idle, 0, 1000});
    }
    if (m_meter2) m_meter2->setSettings({MultimeterService::Mode::Idle, 0, 1000});
    m_meterWatchdog.stop();
    m_meterWatchdog2.stop();
    m_scopeAccepting = true;
    m_scopeAccepting2 = true;
    m_scopeRunning = m_scopeTriggerSource == 0;
    m_scopeSingle = false;
    m_scopeValues.clear();
    m_scopePendingValues.clear();
    m_scopeValues2.clear();
    m_scopePendingValues2.clear();
    emit scopeSamplesChanged();
    emit scopeStateChanged();
    m_feature = QStringLiteral("scope"); emit activeFeatureChanged();
    activateCurrentFeature();
}

void PokitController::activateCurrentFeature()
{
    if (!m_connected && !m_connected2) { connectDevice(); return; }
    if (m_feature == QStringLiteral("meter")) {
        ensureMeter(); ensureMeter2(); startMeter(); startMeter2();
    } else {
        ensureDso(); ensureDso2(); startScope(); startScope2();
    }
}

void PokitController::setMeterMode(int channel, const QString &mode)
{
    if (channel != 1 && channel != 2) return;
    m_meterModeName[channel - 1] = mode;
    if (m_feature == QStringLiteral("meter")) configureMeter(channel == 2);
}

void PokitController::setMeterInterval(int milliseconds)
{
    m_meterInterval = qBound(250, milliseconds, 10000);
    if (m_feature == QStringLiteral("meter")) { startMeter(); startMeter2(); }
}

void PokitController::startMeter()
{
    configureMeter(false);
}

void PokitController::startMeter2()
{
    configureMeter(true);
}

void PokitController::configureMeter(bool second)
{
    if (!(second ? m_connected2 : m_connected)) return;
    auto &watchdog = second ? m_meterWatchdog2 : m_meterWatchdog;
    if (!(second ? m_meterReady2 : m_meterReady)) {
        if (!watchdog.isActive()) watchdog.start(20000);
        return;
    }
    using Mode = MultimeterService::Mode;
    using Position = StatusService::SwitchPosition;
    const auto position = m_meterSwitchPosition[second];
    const QString &requested = m_meterModeName[second];
    const bool ac = requested.startsWith(QStringLiteral("AC"));
    const bool current = requested.endsWith(QStringLiteral("Current"));
    Mode mode;
    quint8 range = 255;
    QString unit;
    if (position == Position::HighCurrent || (current && position != Position::Voltage)) {
        mode = ac ? Mode::AcCurrent : Mode::DcCurrent;
        unit = ac ? QStringLiteral("A AC") : QStringLiteral("A DC");
        if (position == Position::HighCurrent) range = +PokitPro::CurrentRange::_10A;
        if (position == Position::MultiMode) range = +PokitPro::CurrentRange::_300mA;
    } else if (position == Position::MultiMode) {
        mode = Mode::Resistance;
        unit = QStringLiteral("Ω");
        if (requested == QStringLiteral("Diode")) {
            mode = Mode::Diode;
            range = 0;
            unit = QStringLiteral("V");
        } else if (requested == QStringLiteral("Capacitance")) {
            mode = Mode::Capacitance;
            unit = QStringLiteral("F");
        } else if (requested == QStringLiteral("Continuity")) {
            mode = Mode::Continuity;
            range = 0;
            unit = QStringLiteral("Durchgang");
        }
    } else {
        mode = ac ? Mode::AcVoltage : Mode::DcVoltage;
        unit = ac ? QStringLiteral("V AC") : QStringLiteral("V DC");
    }
    auto &valid = second ? m_meterValid2 : m_meterValid;
    if (m_configuredMeterMode[second] != mode) {
        (second ? m_meterValues2 : m_meterValues).clear();
        emit meterHistoryChanged();
    }
    m_configuredMeterMode[second] = mode;
    (second ? m_meterUnit2 : m_meterUnit) = unit;
    m_meterTools.setUnit(second ? 2 : 1, unit);
    valid = false;
    (second ? m_meterOverrange2 : m_meterOverrange) = false;
    emit meterValueChanged();
    (second ? m_meter2 : m_meter)->setSettings({mode, range, quint32(m_meterInterval)});
    watchdog.start(5000);
}

void PokitController::setScopeTrigger(const QString &mode, double level)
{
    if (m_triggerMode == mode && m_triggerLevel == level) return;
    if (m_scopeTriggerSource && m_scopeRunning && !m_rfContinuous) {
        m_rfRestart.stop();
        stopRfCapture();
    }
    m_triggerMode = mode; m_triggerLevel = level;
    emit scopeStateChanged();
    m_scopeRestartTimer.stop();
    m_scopeRestartTimer2.stop();
    if (m_feature == QStringLiteral("scope") && m_scopeRunning) {
        if (m_scopeTriggerSource) {
            if (m_rfStage == RfStage::Idle) m_rfRestart.start();
        }
        else { startScope(); startScope2(); }
    }
}

void PokitController::setScopeConfig(const QString &coupling, double voltsPerDiv, int timePerDivUs)
{
    if (m_scopeCoupling == coupling && scopeVoltsPerDiv() == voltsPerDiv
        && m_scopeTimePerDivUs == timePerDivUs) return;
    if (m_scopeTriggerSource && m_scopeRunning && !m_rfContinuous) {
        m_rfRestart.stop();
        stopRfCapture();
    }
    m_scopeCoupling = coupling;
    if (std::isfinite(voltsPerDiv) && voltsPerDiv > 0)
        m_scopeChannels.setScale(1, voltsPerDiv, m_scopeChannels.at(1).offset);
    m_scopeTimePerDivUs = qBound(10, timePerDivUs, 1000000);
    emit scopeStateChanged();
    m_scopeRestartTimer.stop();
    m_scopeRestartTimer2.stop();
    if (m_feature == QStringLiteral("scope") && m_scopeRunning) {
        if (m_scopeTriggerSource) {
            if (m_rfStage == RfStage::Idle) m_rfRestart.start();
        }
        else { startScope(); startScope2(); }
    }
}

QVariantList PokitController::scopeChannels() const
{
    auto result=m_scopeChannels.values();
    for(int i=0;i<2;++i){
        auto c=result[i].toMap();const auto &q=m_switchQuery[i];
        c.insert("notice",scopeChannelNotice(i+1));
        c.insert("maxScale",m_scopeChannels.at(i+1).current?(m_meterSwitchPosition[i]==StatusService::SwitchPosition::HighCurrent?2.5:.075):100.0);
        c.insert("switch",i?meterSwitch2():meterSwitch());
        c.insert("device_id",i?m_deviceId2:m_deviceId);c.insert("color",i?deviceColor2():deviceColor());
        c.insert("switchPending",q.pending||q.subscribing);c.insert("switchFailures",q.failures);
        c.insert("switchNotifications",q.notifications);c.insert("switchReads",q.reads);c.insert("switchUpdates",q.updates);
        c.insert("switchAgeMs",q.confirmedAt<0?-1:m_switchClock.elapsed()-q.confirmedAt);
        c.insert("switchWarning",q.failures&&!q.uncertain?QStringLiteral("Status wird erneut geprüft; letzter bestätigter Schalterstand wird verwendet"):q.error);
        result[i]=c;
    }
    return result;
}

QString PokitController::scopeChannelNotice(int channel) const
{
    if(channel<1||channel>2)return {};
    const auto position=m_meterSwitchPosition[channel-1];
    if(m_scopeChannels.at(channel).current){
        if(m_switchQuery[channel-1].uncertain)return QStringLiteral("CH%1: Schalterabfrage fehlgeschlagen; warte auf Bestätigung").arg(channel);
        if(!position)return QStringLiteral("CH%1: Schalterstellung für Strommessung wird abgefragt").arg(channel);
        if(position==StatusService::SwitchPosition::Voltage)return QStringLiteral("CH%1: Für Strommessung den Pokit-Schalter auf mA oder A stellen").arg(channel);
        if(ScopeChannels::currentRange(m_scopeChannels.at(channel).peak(),position==StatusService::SwitchPosition::HighCurrent)<0)
            return QStringLiteral("CH%1: Stromskala verkleinern oder passenden A-Schalterbereich wählen").arg(channel);
    }else if(position&&position!=StatusService::SwitchPosition::Voltage)
        return QStringLiteral("CH%1: Schalter auf Spannung stellen oder Messgröße Strom wählen").arg(channel);
    return {};
}

void PokitController::clearScopeChannel(int channel)
{
    if(channel==1){m_scopeValues.clear();m_scopePendingValues.clear();m_scopeAccepting=false;
        m_scopeMinimum=m_scopeMaximum=m_scopeVpp=m_scopeMean=m_scopeRms=m_scopeFrequency=m_scopePeriod=m_scopeCycles=m_scopeDutyCycle=0;
    }else{m_scopeValues2.clear();m_scopePendingValues2.clear();m_scopeAccepting2=false;
        m_scopeMinimum2=m_scopeMaximum2=m_scopeVpp2=m_scopeMean2=m_scopeRms2=m_scopeFrequency2=m_scopePeriod2=m_scopeCycles2=m_scopeDutyCycle2=0;
    }
    emit scopeSamplesChanged();emit scopeMetricsChanged();
}

void PokitController::scopeSwitchChanged(int channel)
{
    const bool running=m_scopeRunning;
    if(m_scopeTriggerSource&&m_rfStage!=RfStage::Idle){stopRfCapture();clearRfPair();}
    clearScopeChannel(channel);
    if(m_scopeChannels.at(channel).current&&m_meterSwitchPosition[channel-1]!=StatusService::SwitchPosition::HighCurrent&&m_scopeChannels.at(channel).peak()>.3)
        m_scopeChannels.setScale(channel,.05,0);
    if(channel==1){m_scopeRestartTimer.stop();m_scopeWatchdog.stop();}else{m_scopeRestartTimer2.stop();m_scopeWatchdog2.stop();}
    m_scopeRunning=running;emit scopeStateChanged();
    if(running&&m_feature==QStringLiteral("scope")){
        if(m_scopeTriggerSource){if(m_rfStage==RfStage::Idle)m_rfRestart.start();}
        else QTimer::singleShot(30,this,[this,channel]{if(m_scopeRunning&&m_feature==QStringLiteral("scope")){if(channel==1)startScope();else startScope2();}});
    }
    publishAutomationState();
}

void PokitController::setScopeChannelCurrent(int channel,bool current)
{
    if(channel<1||channel>2||!m_scopeChannels.setCurrent(channel,current))return;
    if(current)m_scopeChannels.setScale(channel,m_meterSwitchPosition[channel-1]==StatusService::SwitchPosition::HighCurrent?2.0:.05,0);
    if(channel==(m_scopeTriggerSource==2?2:1))m_triggerLevel=0;
    if(m_scopeTriggerSource)clearRfPair();
    scopeSwitchChanged(channel);
}

void PokitController::setScopeChannelVisible(int channel, bool visible)
{
    // Visibility is a display choice: radio pairing and trigger acquisition continue.
    if (!m_scopeChannels.setVisible(channel, visible)) return;
    emit scopeStateChanged();
    publishAutomationState();
}

void PokitController::setScopeChannelScale(int channel, double voltsPerDiv, double offset)
{
    if (channel < 1 || channel > 4) return;
    if (channel<=2 && m_scopeChannels.at(channel).current && voltsPerDiv*4>(m_meterSwitchPosition[channel-1]==StatusService::SwitchPosition::HighCurrent?10.0:.3)) {
        setStatus(QStringLiteral("Stromskala zu groß für die Schalterstellung; passenden A-Bereich wählen"));return;
    }
    const double previousScale = m_scopeChannels.at(channel).voltsPerDiv;
    if (!m_scopeChannels.setScale(channel, voltsPerDiv, offset)) return;
    emit scopeStateChanged();
    // Offset only moves the displayed zero. A changed scale also selects the
    // device range. Freeze radio settings until the current pair completes.
    if (previousScale != m_scopeChannels.at(channel).voltsPerDiv && channel <= 2
        && m_feature == QStringLiteral("scope") && m_scopeRunning) {
        if (m_scopeTriggerSource) {
            if (!m_rfContinuous) {m_rfRestart.stop();stopRfCapture();}
            if (m_rfStage == RfStage::Idle) m_rfRestart.start();
        } else if (channel == 1) {m_scopeRestartTimer.stop();startScope();}
        else {m_scopeRestartTimer2.stop();startScope2();}
    }
    publishAutomationState();
}

void PokitController::setScopeRunning(bool running)
{
    if (m_scopeTriggerSource) {
        if (running && !m_rfContinuous) m_rfRecoveries = 0;
        m_rfContinuous = running;
        if (running) startRfCapture();
        else {
            m_rfRestart.stop();
            stopRfCapture();
            m_scopeRunning = false;
            emit scopeStateChanged();
            setStatus(QStringLiteral("Funkaufnahme angehalten"));
            publishAutomationState();
        }
        return;
    }
    m_scopeRunning = running;
    m_scopeSingle = false;
    emit scopeStateChanged();
    if ((!m_connected || !m_dsoReady) && (!m_connected2 || !m_dsoReady2)) return;
    if (running) { startScope(); startScope2(); }
    else {
        m_scopeWatchdog.stop();
        m_scopeWatchdog2.stop();
        m_scopeRestartTimer.stop();
        m_scopeRestartTimer2.stop();
        m_scopeAccepting = false;
        m_scopeAccepting2 = false;
        if (m_dso && m_dsoReady)
            m_dso->setSettings({DsoService::Command::FreeRunning, 0.0f, DsoService::Mode::Idle,
                                +PokitPro::VoltageRange::_10V, 100000, 1000});
        if (m_dso2 && m_dsoReady2)
            m_dso2->setSettings({DsoService::Command::FreeRunning, 0.0f, DsoService::Mode::Idle,
                                 +PokitPro::VoltageRange::_10V, 100000, 1000});
        setStatus(QStringLiteral("Scope angehalten"));
    }
}

void PokitController::scopeSingle()
{
    if (m_scopeTriggerSource) {
        m_rfContinuous = false;
        m_rfRestart.stop();
        startRfCapture();
        return;
    }
    m_scopeSingle = true;
    m_scopeRunning = true;
    emit scopeStateChanged();
    startScope(); startScope2();
}

DsoService::Settings PokitController::scopeSettings(int channel) const
{
    DsoService::Command command = DsoService::Command::FreeRunning;
    if (m_triggerMode == QStringLiteral("rising")) command = DsoService::Command::RisingEdgeTrigger;
    else if (m_triggerMode == QStringLiteral("falling")) command = DsoService::Command::FallingEdgeTrigger;
    const auto &config=m_scopeChannels.at(channel);
    const double requiredPeak = config.peak();
    quint8 range = +PokitPro::VoltageRange::_600V;
    if (requiredPeak <= 0.25) range = +PokitPro::VoltageRange::_250mV;
    else if (requiredPeak <= 2.0) range = +PokitPro::VoltageRange::_2V;
    else if (requiredPeak <= 10.0) range = +PokitPro::VoltageRange::_10V;
    else if (requiredPeak <= 30.0) range = +PokitPro::VoltageRange::_30V;
    else if (requiredPeak <= 60.0) range = +PokitPro::VoltageRange::_60V;
    else if (requiredPeak <= 125.0) range = +PokitPro::VoltageRange::_125V;
    else if (requiredPeak <= 400.0) range = +PokitPro::VoltageRange::_400V;
    const auto mode = config.current
        ? (m_scopeCoupling == QStringLiteral("AC") ? DsoService::Mode::AcCurrent : DsoService::Mode::DcCurrent)
        : (m_scopeCoupling == QStringLiteral("AC") ? DsoService::Mode::AcVoltage : DsoService::Mode::DcVoltage);
    if(config.current)range=quint8(ScopeChannels::currentRange(requiredPeak,m_meterSwitchPosition[channel-1]==StatusService::SwitchPosition::HighCurrent));
    const float safeTrigger = float(qBound(-requiredPeak, m_triggerLevel, requiredPeak));
    const quint32 windowUs = quint32(m_scopeTimePerDivUs * 10);
    // Pokit Pro tops out at 1 MS/s. Reduce record length for the fastest
    // time bases instead of asking the firmware for an impossible rate.
    const quint16 samples = quint16(qBound(100u, windowUs, 1000u));
    return {command, safeTrigger, mode, range, windowUs, samples};
}

void PokitController::resumeAfterSwitch(int channel)
{
    QTimer::singleShot(50,this,[this,channel]{
        if(m_feature!=QStringLiteral("scope")||!m_scopeRunning)return;
        if(m_scopeTriggerSource){if(m_rfStage==RfStage::Idle)startRfCapture();}
        else if(channel==1)startScope();else startScope2();
    });
}

void PokitController::failSwitchQuery(int channel,quint64 request,const QString &reason)
{
    const int i=channel-1;auto &q=m_switchQuery[i];
    if(!q.fail(request,m_switchClock.elapsed(),m_meterSwitchPosition[i].has_value(),reason))return;
    qWarning()<<"Switch CH"<<channel<<reason<<"failures"<<q.failures<<"notifications"<<q.notifications;
    emit scopeStateChanged();publishAutomationState();
    if(q.uncertain&&m_scopeChannels.at(channel).current)setStatus(scopeChannelNotice(channel));
    if(q.failures==3){
        setStatus(QStringLiteral("CH%1: Statusdienst wird durch Neuverbinden wiederhergestellt").arg(channel));
        if(i?m_connected2:m_connected)(i?m_device2:m_device)->controller()->disconnectFromDevice();
    }else resumeAfterSwitch(channel);
}

bool PokitController::refreshScopeSwitch(int channel)
{
    const int i=channel-1;auto &q=m_switchQuery[i];
    if(q.pending||q.subscribing)return true;
    if(!m_meterStatusReady[i]||!(i?m_connected2:m_connected))return false;
    auto *status=m_meterStatus[i];
    if(q.canNotify&&!q.subscriptionAttempted){
        q.subscriptionAttempted=true;q.subscribing=true;const auto request=++q.token;
        if(!status->enableStatusNotifications()){q.subscribing=false;q.notifications=false;}
        else{
            QTimer::singleShot(4500,this,[this,i,channel,request]{auto &query=m_switchQuery[i];
                if(!query.subscribing||query.token!=request)return;
                query.subscribing=false;query.notifications=false;
                query.error=QStringLiteral("Status-Benachrichtigung nicht bestätigt; Leseabfrage wird verwendet");
                resumeAfterSwitch(channel);emit scopeStateChanged();
            });return true;
        }
    }
    if(!q.begin(m_switchClock.elapsed()))return false;
    const auto request=q.token;
    if(!status->readStatusCharacteristic()){
        failSwitchQuery(channel,request,QStringLiteral("Status-Leseauftrag konnte nicht gestartet werden"));return true;
    }
    QTimer::singleShot(4500,this,[this,channel,request]{failSwitchQuery(channel,request,QStringLiteral("Keine Statusantwort innerhalb von 4,5 s"));});
    return true;
}

void PokitController::startScope()
{
    if (!m_connected || !m_dsoReady || !m_scopeRunning || m_scopeTriggerSource) return;
    if(refreshScopeSwitch(1))return;
    if(!scopeChannelNotice(1).isEmpty()){m_scopeWatchdog.stop();setStatus(scopeChannelNotice(1));return;}
    const auto settings = scopeSettings();
    m_expectedScopeWindowUs = settings.samplingWindow;
    m_expectedScopeSamples = settings.numberOfSamples;
    m_scopeAccepting = false;
    m_scopePendingValues.clear();
    m_dso->setSettings(settings);
    m_scopeWatchdog.start();
    if (m_scopeValues.isEmpty())
        setStatus(QStringLiteral("Scope wird gestartet …"));
}

void PokitController::startScope2()
{
    if (!m_connected2 || !m_dsoReady2 || !m_scopeRunning || m_scopeTriggerSource) return;
    if(refreshScopeSwitch(2))return;
    if(!scopeChannelNotice(2).isEmpty()){m_scopeWatchdog2.stop();setStatus(scopeChannelNotice(2));return;}
    const auto settings = scopeSettings(2);
    m_expectedScopeWindowUs2 = settings.samplingWindow;
    m_expectedScopeSamples2 = settings.numberOfSamples;
    m_scopeAccepting2 = false;
    m_scopePendingValues2.clear();
    m_dso2->setSettings(settings);
    m_scopeWatchdog2.start();
}

namespace {
constexpr quint16 rfSamples = 1000;

bool isRadioCapable(const PokitDevice *device)
{
    const auto *controller = device->controller();
    if (controller->services().contains(StatusService::ServiceUuids::pokitPro)) return true;
    // WinRT can expose the DSO before it returns the product-specific status service.
    return controller->remoteName().simplified().remove(u' ').compare(
        QStringLiteral("PokitPro"), Qt::CaseInsensitive) == 0;
}
}

bool PokitController::scopeRfReady() const
{
    return m_connected && m_connected2 && m_dsoReady && m_dsoReady2
        && m_rfNotificationsReady[0] && m_rfNotificationsReady[1]
        && isRadioCapable(m_device) && isRadioCapable(m_device2);
}

void PokitController::setScopeTriggerSource(int channel)
{
    if (channel < 0 || channel > 2 || channel == m_scopeTriggerSource) return;
    if(m_scopeChannels.at(m_scopeTriggerSource==2?2:1).current!=m_scopeChannels.at(channel==2?2:1).current)m_triggerLevel=0;
    stopRfCapture();
    m_rfContinuous = false;
    m_rfRestart.stop();
    m_scopeRunning = false;
    m_scopeSingle = false;
    m_scopeAccepting = m_scopeAccepting2 = false;
    m_scopeWatchdog.stop();
    m_scopeWatchdog2.stop();
    m_scopeRestartTimer.stop();
    m_scopeRestartTimer2.stop();
    m_scopeTriggerSource = channel;
    clearRfPair();
    m_scopeValues.clear();
    m_scopeValues2.clear();
    emit scopeSamplesChanged();
    emit scopeStateChanged();
    if (channel == 0 && m_feature == QStringLiteral("scope")) {
        setScopeRunning(true);
    } else if (channel) {
        setStatus(QStringLiteral("Funktrigger CH%1 · SINGLE startet ein Aufnahmepaar").arg(channel));
    }
    publishAutomationState();
}

void PokitController::setScopeTriggerPosition(double position)
{
    if (!std::isfinite(position)) return;
    m_scopeTriggerPosition = qBound(0.0, position, 1.0);
}

void PokitController::clearRfPair()
{
    if (!m_scopeRfPair) return;
    m_scopeRfPair = false;
    m_scopeValues.clear();
    m_scopeValues2.clear();
    emit scopeStateChanged();
    emit scopeSamplesChanged();
}

void PokitController::startRfCapture()
{
    if (m_rfStage != RfStage::Idle || m_feature != QStringLiteral("scope")) return;
    // Remember SINGLE/RUN intent while an asynchronous switch read completes.
    m_scopeRunning=true;emit scopeStateChanged();
    if(refreshScopeSwitch(1)||refreshScopeSwitch(2))return;
    for(int channel=1;channel<=2;++channel)if(!scopeChannelNotice(channel).isEmpty()){
        m_scopeRunning=true;emit scopeStateChanged();setStatus(scopeChannelNotice(channel));return;
    }
    if (!scopeRfReady()) {
        m_rfContinuous = false;
        m_scopeRunning = false;
        emit scopeStateChanged();
        setStatus(QStringLiteral("Funktrigger benötigt zwei messbereite Pokit Pro"));
        return;
    }
    const auto settings = scopeSettings();
    // Keep the most recent complete pair visible while the next trigger is pending.
    m_dso->setPokitProduct(PokitProduct::PokitPro);
    m_dso2->setPokitProduct(PokitProduct::PokitPro);
    m_rfSettings = settings;
    m_rfSettings2 = scopeSettings(2);
    // A fresh radio group filters late data and acknowledgements from prior shots.
    m_rfGroup = m_rfGroup ? quint8(m_rfGroup % 127 + 1)
                          : quint8(QRandomGenerator::global()->bounded(1, 128));
    m_scopeRfTriggerSample = settings.command == DsoService::Command::FreeRunning
        ? 0 : quint16(qRound(m_scopeTriggerPosition * m_rfSettings.numberOfSamples));
    m_rfFrames[0] = {};
    m_rfFrames[1] = {};
    m_rfResends = 0;
    m_scopeRunning = true;
    m_rfStage = RfStage::ArmingFollower;
    emit scopeStateChanged();
    setStatus(QStringLiteral("Funktrigger: Empfänger wird scharfgestellt"));
    m_rfTimeout.start(15000);
    auto *follower = m_scopeTriggerSource == 1 ? m_dso2 : m_dso;
    if (!follower->setSynchronizedSettings(m_scopeTriggerSource == 1 ? m_rfSettings2 : m_rfSettings, m_scopeRfTriggerSample,
                                           DsoService::SyncRole::Follower, m_rfGroup)) {
        failRfCapture(QStringLiteral("Funktrigger: Empfänger konnte nicht scharfgestellt werden"));
    }
    publishAutomationState();
}

void PokitController::onRfSettingsWritten(int channel, DsoService::SyncRole role, quint8 group)
{
    if (m_rfStage != RfStage::ArmingFollower || channel == m_scopeTriggerSource
        || role != DsoService::SyncRole::Follower || group != m_rfGroup) return;
    m_rfStage = RfStage::WaitingForFrames;
    setStatus(QStringLiteral("Funktrigger: warte auf CH%1 und das Aufnahmepaar").arg(m_scopeTriggerSource));
    m_rfTimeout.start(15000 + int(m_rfSettings.samplingWindow / 1000));
    auto *master = m_scopeTriggerSource == 1 ? m_dso : m_dso2;
    if (!master->setSynchronizedSettings(m_scopeTriggerSource == 1 ? m_rfSettings : m_rfSettings2, m_scopeRfTriggerSample,
                                         DsoService::SyncRole::Master, m_rfGroup)) {
        failRfCapture(QStringLiteral("Funktrigger: Quelle konnte nicht scharfgestellt werden"));
    }
}

void PokitController::onRfMetadata(int channel, const DsoService::Metadata &metadata)
{
    if (m_rfStage == RfStage::WaitingForFrames) {
        qInfo() << "RF metadata CH" << channel << "status" << int(metadata.status)
                << "window" << metadata.samplingWindow << "samples" << metadata.numberOfSamples
                << "group" << metadata.syncGroup.value_or(255) << "expected" << m_rfGroup
                << "role" << int(metadata.syncRole.value_or(DsoService::SyncRole::Independent));
    }
    if (m_rfStage != RfStage::WaitingForFrames || metadata.status != DsoService::DsoStatus::Done
        || metadata.mode != (channel==1?m_rfSettings:m_rfSettings2).mode || metadata.samplingWindow != m_rfSettings.samplingWindow
        || metadata.numberOfSamples != m_rfSettings.numberOfSamples
        || !metadata.samplingRate || !std::isfinite(metadata.scale)) return;
    const auto expectedRole = channel == m_scopeTriggerSource
        ? DsoService::SyncRole::Master : DsoService::SyncRole::Follower;
    if (metadata.syncRole != expectedRole || metadata.syncGroup != m_rfGroup) return;
    if (!metadata.captureSequence || !metadata.triggerSample) {
        failRfCapture(QStringLiteral("Funktrigger: Firmware liefert keine eindeutige Aufnahmenummer"));
        return;
    }
    if (*metadata.triggerSample > metadata.numberOfSamples) {
        failRfCapture(QStringLiteral("Funktrigger: ungültige Triggerposition"));
        return;
    }
    auto &frame = m_rfFrames[channel - 1];
    if (!frame.metadata) {
        frame.metadata = metadata;
        frame.values.reserve(metadata.numberOfSamples);
    } else if (frame.metadata->captureSequence != metadata.captureSequence) {
        failRfCapture(QStringLiteral("Funktrigger: mehrere Aufnahmen auf demselben Kanal"));
    }
}

void PokitController::onRfSamples(int channel, const DsoService::Samples &samples)
{
    if (m_rfStage != RfStage::WaitingForFrames) return;
    auto &frame = m_rfFrames[channel - 1];
    if (!frame.metadata || frame.values.size() == frame.metadata->numberOfSamples) return;
    if (frame.values.size() + samples.size() > frame.metadata->numberOfSamples) {
        failRfCapture(QStringLiteral("Funktrigger: unerwartete Sample-Anzahl"));
        return;
    }
    for (qint16 value : samples) frame.values.append(value * frame.metadata->scale);

    const auto &first = m_rfFrames[0];
    const auto &second = m_rfFrames[1];
    if (!first.metadata || !second.metadata
        || first.values.size() != first.metadata->numberOfSamples
        || second.values.size() != second.metadata->numberOfSamples) return;
    if (first.metadata->captureSequence != second.metadata->captureSequence) {
        failRfCapture(QStringLiteral("Funktrigger: Aufnahmenummern stimmen nicht überein"));
        return;
    }

    m_rfTimeout.stop();
    m_rfStage = RfStage::Idle;
    m_scopeMetadata = *first.metadata;
    m_scopeMetadata2 = *second.metadata;
    m_scopeValues = first.values;
    m_scopeValues2 = second.values;
    m_scopeRfPair = true;
    m_rfRecoveries = 0;
    m_scopeRunning = m_rfContinuous;
    updateScopeMetrics();
    updateScopeMetrics2();
    emit scopeStateChanged();
    emit scopeSamplesChanged();
    setStatus(QStringLiteral("Funktrigger CH%1 · Aufnahmepaar %2 · %3 Samples je Kanal")
              .arg(m_scopeTriggerSource).arg(*m_scopeMetadata.captureSequence).arg(m_scopeValues.size()));
    publishAutomationState();
    if (m_rfContinuous) m_rfRestart.start();
}

void PokitController::stopRfCapture()
{
    if (m_rfStage == RfStage::Idle) return;
    m_rfStage = RfStage::Idle;
    m_rfTimeout.stop();
    const DsoService::Settings stop{DsoService::Command::Stop, 0.0f, DsoService::Mode::Idle,
                                    m_rfSettings.range, 5000, rfSamples};
    if (m_connected && m_dsoReady)
        m_dso->setSynchronizedSettings(stop, 500, DsoService::SyncRole::Independent, 0);
    if (m_connected2 && m_dsoReady2)
        m_dso2->setSynchronizedSettings(stop, 500, DsoService::SyncRole::Independent, 0);
    m_rfFrames[0] = {};
    m_rfFrames[1] = {};
}

void PokitController::failRfCapture(const QString &message)
{
    if (m_rfContinuous && scopeRfReady() && m_rfRecoveries < 3) {
        ++m_rfRecoveries;
        stopRfCapture();
        m_rfRestart.start(1000);
        setStatus(QStringLiteral("%1 · automatischer Neuversuch %2/3").arg(message).arg(m_rfRecoveries));
        publishAutomationState();
        return;
    }
    m_rfContinuous = false;
    m_rfRestart.stop();
    stopRfCapture();
    m_scopeRunning = false;
    setStatus(message);
    emit scopeStateChanged();
    publishAutomationState();
}

