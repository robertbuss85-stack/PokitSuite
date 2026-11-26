#include "pokitcontroller.h"

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
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLowEnergyController>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPen>
#include <QSaveFile>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <algorithm>
#include <cmath>

static QString deviceKey(const QBluetoothDeviceInfo &info)
{
    if (!info.deviceUuid().isNull())
        return info.deviceUuid().toString(QUuid::WithoutBraces);
    if (!info.address().isNull()) return info.address().toString();
    return {};
}

PokitController::PokitController(QObject *parent) : QObject(parent)
{
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
    connect(&m_rfTimeout, &QTimer::timeout, this, [this] {
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
        if (m_feature != QStringLiteral("meter")) return;
        for (int channel = 0; channel < 2; ++channel) {
            if (!(channel ? m_connected2 : m_connected)) continue;
            if (m_meterStatusReady[channel]) m_meterStatus[channel]->readStatusCharacteristic();
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
    m_meterStatus[second] = status;
    connect(status, &AbstractPokitService::serviceDetailsDiscovered, this, [this, second, status] {
        m_meterStatusReady[second] = true;
        if (m_feature == QStringLiteral("meter")) status->readStatusCharacteristic();
    });
    connect(status, &StatusService::deviceStatusRead, this, [this, second](const StatusService::Status &reading) {
        if (!(second ? m_connected2 : m_connected)
            || reading.switchPosition == m_meterSwitchPosition[second]) return;
        m_meterSwitchPosition[second] = reading.switchPosition;
        if (m_feature == QStringLiteral("meter")) configureMeter(second);
        emit meterValueChanged();
    });
}

void PokitController::ensureDso()
{
    if (!m_device || m_dso) return;
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
        if (meta.samplingWindow != m_expectedScopeWindowUs ||
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
        if (meta.samplingWindow != m_expectedScopeWindowUs2 ||
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
        entry.insert(QStringLiteral("sampleCount"), object.value(QStringLiteral("waveform_v")).toArray().size());
        entry.insert(QStringLiteral("vpp"), metrics.value(QStringLiteral("vpp_v")).toDouble());
        entry.insert(QStringLiteral("frequency"), metrics.value(QStringLiteral("frequency_hz")).toDouble());
        captures.append(entry);
    }
    m_savedCaptures = captures;
    emit savedCapturesChanged();
}

bool PokitController::writeCapturePdf(const QString &path, const QJsonObject &capture) const
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(300);
    writer.setTitle(QStringLiteral("PokitSuite Oscilloscope Capture"));
    writer.setCreator(QStringLiteral("PokitSuite"));
    QPainter painter(&writer);
    if (!painter.isActive()) return false;

    const int pageWidth = writer.width();
    const int margin = 180;
    const QRect content(margin, margin, pageWidth - margin * 2, writer.height() - margin * 2);
    painter.fillRect(QRect(0, 0, writer.width(), writer.height()), QColor(QStringLiteral("#ffffff")));

    QFont titleFont(QStringLiteral("Segoe UI"), 22, QFont::Bold);
    QFont sectionFont(QStringLiteral("Segoe UI"), 12, QFont::Bold);
    QFont bodyFont(QStringLiteral("Segoe UI"), 9);
    painter.setFont(titleFont);
    painter.setPen(QColor(QStringLiteral("#12212b")));
    painter.drawText(QRect(content.left(), content.top(), content.width(), 120),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("PokitSuite Oscilloscope Report"));
    painter.setFont(bodyFont);
    painter.setPen(QColor(QStringLiteral("#4d626e")));
    painter.drawText(content.left(), content.top() + 135,
                     QStringLiteral("Capture: %1").arg(capture.value(QStringLiteral("display_name")).toString()));
    painter.drawText(content.left(), content.top() + 185,
                     QStringLiteral("Device: %1").arg(capture.value(QStringLiteral("device")).toString()));

    const QRect plot(content.left(), content.top() + 270, content.width(), 1180);
    painter.fillRect(plot, QColor(QStringLiteral("#07100b")));
    painter.setPen(QPen(QColor(QStringLiteral("#294a34")), 2));
    for (int i = 0; i <= 10; ++i) {
        const int x = plot.left() + i * plot.width() / 10;
        painter.drawLine(x, plot.top(), x, plot.bottom());
    }
    for (int i = 0; i <= 8; ++i) {
        const int y = plot.top() + i * plot.height() / 8;
        painter.drawLine(plot.left(), y, plot.right(), y);
    }
    painter.setPen(QPen(QColor(QStringLiteral("#6b8171")), 3));
    painter.drawLine(plot.center().x(), plot.top(), plot.center().x(), plot.bottom());
    painter.drawLine(plot.left(), plot.center().y(), plot.right(), plot.center().y());

    const QJsonArray waveform = capture.value(QStringLiteral("waveform_v")).toArray();
    const QJsonObject settings = capture.value(QStringLiteral("settings")).toObject();
    const double voltsPerDiv = qMax(0.001, settings.value(QStringLiteral("volts_per_div")).toDouble(1.0));
    if (waveform.size() > 1) {
        QPainterPath pathShape;
        for (int index = 0; index < waveform.size(); ++index) {
            const double x = plot.left() + double(index) * plot.width() / double(waveform.size() - 1);
            const double y = plot.center().y() - waveform.at(index).toDouble() / voltsPerDiv * plot.height() / 8.0;
            if (index == 0) pathShape.moveTo(x, y); else pathShape.lineTo(x, y);
        }
        painter.save();
        painter.setClipRect(plot.adjusted(2, 2, -2, -2));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#ffd629")), 6));
        painter.drawPath(pathShape);
        painter.restore();
    }
    painter.setFont(sectionFont);
    painter.setPen(QColor(QStringLiteral("#12212b")));
    painter.drawText(content.left(), plot.bottom() + 100, QStringLiteral("Measured and derived values"));

    const QJsonObject metrics = capture.value(QStringLiteral("metrics")).toObject();
    struct ReportRow { QString label; QString value; };
    const QList<ReportRow> rows{
        {QStringLiteral("Minimum"), QString::number(metrics.value(QStringLiteral("minimum_v")).toDouble(), 'f', 6) + QStringLiteral(" V")},
        {QStringLiteral("Maximum"), QString::number(metrics.value(QStringLiteral("maximum_v")).toDouble(), 'f', 6) + QStringLiteral(" V")},
        {QStringLiteral("Peak-to-peak"), QString::number(metrics.value(QStringLiteral("vpp_v")).toDouble(), 'f', 6) + QStringLiteral(" V")},
        {QStringLiteral("Mean"), QString::number(metrics.value(QStringLiteral("mean_v")).toDouble(), 'f', 6) + QStringLiteral(" V")},
        {QStringLiteral("RMS"), QString::number(metrics.value(QStringLiteral("rms_v")).toDouble(), 'f', 6) + QStringLiteral(" V")},
        {QStringLiteral("Frequency"), QString::number(metrics.value(QStringLiteral("frequency_hz")).toDouble(), 'f', 3) + QStringLiteral(" Hz")},
        {QStringLiteral("Period"), QString::number(metrics.value(QStringLiteral("period_s")).toDouble(), 'g', 7) + QStringLiteral(" s")},
        {QStringLiteral("Cycles"), QString::number(metrics.value(QStringLiteral("cycles")).toDouble(), 'f', 0)},
        {QStringLiteral("Duty cycle"), QString::number(metrics.value(QStringLiteral("duty_cycle_percent")).toDouble(), 'f', 2) + QStringLiteral(" %")},
        {QStringLiteral("Sample rate"), QString::number(settings.value(QStringLiteral("sample_rate_hz")).toDouble(), 'f', 0) + QStringLiteral(" S/s")},
        {QStringLiteral("Sample count"), QString::number(waveform.size())},
        {QStringLiteral("Time / div"), QString::number(settings.value(QStringLiteral("time_per_div_us")).toInt()) + QStringLiteral(" us")},
        {QStringLiteral("Voltage / div"), QString::number(voltsPerDiv, 'g', 6) + QStringLiteral(" V")},
        {QStringLiteral("Coupling"), settings.value(QStringLiteral("coupling")).toString()},
        {QStringLiteral("Trigger"), settings.value(QStringLiteral("trigger_mode")).toString() + QStringLiteral(" at ") +
            QString::number(settings.value(QStringLiteral("trigger_level_v")).toDouble(), 'f', 3) + QStringLiteral(" V")}
    };
    const int tableTop = plot.bottom() + 150;
    const int rowHeight = 88;
    const int labelWidth = content.width() * 46 / 100;
    painter.setFont(bodyFont);
    for (int index = 0; index < rows.size(); ++index) {
        const QRect row(content.left(), tableTop + index * rowHeight, content.width(), rowHeight);
        painter.fillRect(row, index % 2 ? QColor(QStringLiteral("#eef3f5")) : QColor(QStringLiteral("#f8fafb")));
        painter.setPen(QColor(QStringLiteral("#c6d0d5")));
        painter.drawRect(row);
        painter.drawLine(row.left() + labelWidth, row.top(), row.left() + labelWidth, row.bottom());
        painter.setPen(QColor(QStringLiteral("#263942")));
        painter.drawText(row.adjusted(22, 0, -22, 0), Qt::AlignLeft | Qt::AlignVCenter, rows[index].label);
        painter.drawText(row.adjusted(labelWidth + 22, 0, -22, 0), Qt::AlignLeft | Qt::AlignVCenter, rows[index].value);
    }
    painter.setPen(QColor(QStringLiteral("#70838d")));
    painter.drawText(QRect(content.left(), content.bottom() - 80, content.width(), 60),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("PokitSuite - generated %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    painter.end();
    return QFileInfo(path).exists() && QFileInfo(path).size() > 0;
}

QString PokitController::saveScopeCapture()
{
    if (m_scopeValues.size() < 2) {
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
    const QString displayName = now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QJsonObject metrics{
        {QStringLiteral("minimum_v"), m_scopeMinimum}, {QStringLiteral("maximum_v"), m_scopeMaximum},
        {QStringLiteral("vpp_v"), m_scopeVpp}, {QStringLiteral("mean_v"), m_scopeMean},
        {QStringLiteral("rms_v"), m_scopeRms}, {QStringLiteral("frequency_hz"), m_scopeFrequency},
        {QStringLiteral("period_s"), m_scopePeriod}, {QStringLiteral("cycles"), m_scopeCycles},
        {QStringLiteral("duty_cycle_percent"), m_scopeDutyCycle}
    };
    QJsonObject settings{
        {QStringLiteral("sample_rate_hz"), double(m_scopeMetadata.samplingRate)},
        {QStringLiteral("sampling_window_us"), double(m_scopeMetadata.samplingWindow)},
        {QStringLiteral("time_per_div_us"), m_scopeTimePerDivUs},
        {QStringLiteral("volts_per_div"), m_scopeVoltsPerDiv},
        {QStringLiteral("coupling"), m_scopeCoupling},
        {QStringLiteral("trigger_mode"), m_triggerMode},
        {QStringLiteral("trigger_level_v"), m_triggerLevel}
    };
    QJsonArray waveform;
    for (double value : m_scopeValues) waveform.append(value);
    QJsonObject capture{
        {QStringLiteral("schema"), QStringLiteral("pokitsuite.scope-capture.v1")},
        {QStringLiteral("display_name"), displayName},
        {QStringLiteral("timestamp_utc"), now.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("device"), m_deviceName}, {QStringLiteral("metrics"), metrics},
        {QStringLiteral("settings"), settings}, {QStringLiteral("waveform_v"), waveform}
    };
    const QString jsonPath = directory.filePath(base + QStringLiteral(".json"));
    QSaveFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly) ||
        jsonFile.write(QJsonDocument(capture).toJson(QJsonDocument::Indented)) < 0 || !jsonFile.commit()) {
        setStatus(QStringLiteral("Messdatei konnte nicht gespeichert werden"));
        return {};
    }
    const QString csvPath = directory.filePath(base + QStringLiteral(".csv"));
    QSaveFile csvFile(csvPath);
    if (csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&csvFile);
        stream << "sample_index,time_s,voltage_v\n";
        const double sampleRate = m_scopeMetadata.samplingRate;
        for (int index = 0; index < m_scopeValues.size(); ++index)
            stream << index << ',' << QString::number(sampleRate > 0 ? index / sampleRate : 0.0, 'g', 12)
                   << ',' << QString::number(m_scopeValues[index], 'g', 12) << '\n';
        csvFile.commit();
    }
    const QString pdfPath = directory.filePath(base + QStringLiteral(".pdf"));
    if (!writeCapturePdf(pdfPath, capture)) {
        setStatus(QStringLiteral("Messwerte gespeichert, PDF-Erstellung fehlgeschlagen"));
        refreshSavedCaptures();
        return {};
    }
    refreshSavedCaptures();
    setStatus(QStringLiteral("Scope-Aufnahme als PDF und Wertetabelle gespeichert"));
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
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray waveform = object.value(QStringLiteral("waveform_v")).toArray();
    QVector<double> values; values.reserve(waveform.size());
    for (const QJsonValue &value : waveform) values.append(value.toDouble());
    if (values.size() < 2) return;
    m_referenceValues = values;
    m_referenceName = object.value(QStringLiteral("display_name")).toString(info.baseName());
    emit referenceChanged();
    setStatus(QStringLiteral("Vergleichskurve geladen: %1").arg(m_referenceName));
}

void PokitController::clearScopeReference()
{
    m_referenceValues.clear();
    m_referenceName.clear();
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
    const QDir root(QDir::current());
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
        {QStringLiteral("sample_rate_hz"), double(m_scopeMetadata.samplingRate)},
        {QStringLiteral("sampling_window_us"), double(m_scopeMetadata.samplingWindow)},
        {QStringLiteral("samples"), m_scopeValues.size()},
        {QStringLiteral("time_per_div_us"), m_scopeTimePerDivUs},
        {QStringLiteral("volts_per_div"), m_scopeVoltsPerDiv},
        {QStringLiteral("coupling"), m_scopeCoupling},
        {QStringLiteral("trigger_mode"), m_triggerMode},
        {QStringLiteral("trigger_level_v"), m_triggerLevel},
        {QStringLiteral("reference_name"), m_referenceName},
        {QStringLiteral("reference_samples"), m_referenceValues.size()},
        {QStringLiteral("metrics"), metrics},
        {QStringLiteral("waveform_v"), waveform}
    };
    QJsonObject scope2{
        {QStringLiteral("sample_rate_hz"), double(m_scopeMetadata2.samplingRate)},
        {QStringLiteral("sampling_window_us"), double(m_scopeMetadata2.samplingWindow)},
        {QStringLiteral("samples"), m_scopeValues2.size()},
        {QStringLiteral("time_per_div_us"), m_scopeTimePerDivUs},
        {QStringLiteral("volts_per_div"), m_scopeVoltsPerDiv},
        {QStringLiteral("coupling"), m_scopeCoupling},
        {QStringLiteral("trigger_mode"), m_triggerMode},
        {QStringLiteral("trigger_level_v"), m_triggerLevel},
        {QStringLiteral("metrics"), metrics2},
        {QStringLiteral("waveform_v"), waveform2}
    };
    QJsonObject state{
        {QStringLiteral("schema"), QStringLiteral("pokitsuite.telemetry.v1")},
        {QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("connected"), m_connected},
        {QStringLiteral("device"), m_deviceName},
        {QStringLiteral("connected2"), m_connected2},
        {QStringLiteral("device2"), m_deviceName2},
        {QStringLiteral("connected_device_count"), (m_connected ? 1 : 0) + (m_connected2 ? 1 : 0)},
        {QStringLiteral("status"), m_status},
        {QStringLiteral("active_feature"), m_feature},
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
    const QDir root(QDir::current());
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
    else if (action == QStringLiteral("save_scope_capture")) saveScopeCapture();
    else if (action == QStringLiteral("load_scope_reference"))
        loadScopeCapture(command.value(QStringLiteral("json_path")).toString());
    else if (action == QStringLiteral("clear_scope_reference")) clearScopeReference();
    else if (action == QStringLiteral("scope_config")) {
        setScopeConfig(command.value(QStringLiteral("coupling")).toString(QStringLiteral("DC")),
                       command.value(QStringLiteral("volts_per_div")).toDouble(m_scopeVoltsPerDiv),
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
    valid = false;
    (second ? m_meterOverrange2 : m_meterOverrange) = false;
    emit meterValueChanged();
    (second ? m_meter2 : m_meter)->setSettings({mode, range, quint32(m_meterInterval)});
    watchdog.start(5000);
}

void PokitController::setScopeTrigger(const QString &mode, double level)
{
    if (m_triggerMode == mode && m_triggerLevel == level) return;
    if (m_rfStage != RfStage::Idle) setScopeRunning(false);
    m_triggerMode = mode; m_triggerLevel = level;
    m_scopeRestartTimer.stop();
    m_scopeRestartTimer2.stop();
    if (m_feature == QStringLiteral("scope") && m_scopeRunning) { startScope(); startScope2(); }
}

void PokitController::setScopeConfig(const QString &coupling, double voltsPerDiv, int timePerDivUs)
{
    if (m_scopeCoupling == coupling && m_scopeVoltsPerDiv == voltsPerDiv
        && m_scopeTimePerDivUs == timePerDivUs) return;
    if (m_rfStage != RfStage::Idle) setScopeRunning(false);
    m_scopeCoupling = coupling;
    m_scopeVoltsPerDiv = qBound(0.01, voltsPerDiv, 100.0);
    m_scopeTimePerDivUs = qBound(10, timePerDivUs, 1000000);
    m_scopeRestartTimer.stop();
    m_scopeRestartTimer2.stop();
    if (m_feature == QStringLiteral("scope") && m_scopeRunning) { startScope(); startScope2(); }
}

void PokitController::setScopeRunning(bool running)
{
    if (m_scopeTriggerSource) {
        if (running) startRfCapture();
        else if (m_rfStage != RfStage::Idle) {
            stopRfCapture();
            m_scopeRunning = false;
            emit scopeStateChanged();
            setStatus(QStringLiteral("Funkaufnahme angehalten"));
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
        startRfCapture();
        return;
    }
    m_scopeSingle = true;
    m_scopeRunning = true;
    emit scopeStateChanged();
    startScope(); startScope2();
}

DsoService::Settings PokitController::scopeSettings() const
{
    DsoService::Command command = DsoService::Command::FreeRunning;
    if (m_triggerMode == QStringLiteral("rising")) command = DsoService::Command::RisingEdgeTrigger;
    else if (m_triggerMode == QStringLiteral("falling")) command = DsoService::Command::FallingEdgeTrigger;
    const double requiredPeak = m_scopeVoltsPerDiv * 4.0;
    quint8 range = +PokitPro::VoltageRange::_600V;
    if (requiredPeak <= 0.25) range = +PokitPro::VoltageRange::_250mV;
    else if (requiredPeak <= 2.0) range = +PokitPro::VoltageRange::_2V;
    else if (requiredPeak <= 10.0) range = +PokitPro::VoltageRange::_10V;
    else if (requiredPeak <= 30.0) range = +PokitPro::VoltageRange::_30V;
    else if (requiredPeak <= 60.0) range = +PokitPro::VoltageRange::_60V;
    else if (requiredPeak <= 125.0) range = +PokitPro::VoltageRange::_125V;
    else if (requiredPeak <= 400.0) range = +PokitPro::VoltageRange::_400V;
    const auto mode = m_scopeCoupling == QStringLiteral("AC")
        ? DsoService::Mode::AcVoltage : DsoService::Mode::DcVoltage;
    const float safeTrigger = float(qBound(-requiredPeak, m_triggerLevel, requiredPeak));
    const quint32 windowUs = quint32(m_scopeTimePerDivUs * 10);
    // Pokit Pro tops out at 1 MS/s. Reduce record length for the fastest
    // time bases instead of asking the firmware for an impossible rate.
    const quint16 samples = quint16(qBound(100u, windowUs, 1000u));
    return {command, safeTrigger, mode, range, windowUs, samples};
}

void PokitController::startScope()
{
    if (!m_connected || !m_dsoReady || !m_scopeRunning || m_scopeTriggerSource) return;
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
    const auto settings = scopeSettings();
    m_expectedScopeWindowUs2 = settings.samplingWindow;
    m_expectedScopeSamples2 = settings.numberOfSamples;
    m_scopeAccepting2 = false;
    m_scopePendingValues2.clear();
    m_dso2->setSettings(settings);
    m_scopeWatchdog2.start();
}

namespace {
constexpr quint16 rfSamples = 1000;
constexpr quint32 rfMinimumWindowUs = 1000;

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
    stopRfCapture();
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
    if (!scopeRfReady()) {
        setStatus(QStringLiteral("Funktrigger benötigt zwei messbereite Pokit Pro"));
        return;
    }
    const auto settings = scopeSettings();
    if (settings.samplingWindow < rfMinimumWindowUs) {
        setStatus(QStringLiteral("Funktrigger: mindestens 100 µs/div einstellen"));
        return;
    }

    clearRfPair();
    m_dso->setPokitProduct(PokitProduct::PokitPro);
    m_dso2->setPokitProduct(PokitProduct::PokitPro);
    m_rfSettings = settings;
    m_rfSettings.numberOfSamples = rfSamples;
    // A fresh radio group filters late data and acknowledgements from prior shots.
    m_rfGroup = m_rfGroup ? quint8(m_rfGroup % 127 + 1)
                          : quint8(QRandomGenerator::global()->bounded(1, 128));
    m_scopeRfTriggerSample = settings.command == DsoService::Command::FreeRunning
        ? 0 : quint16(qRound(m_scopeTriggerPosition * rfSamples));
    m_rfFrames[0] = {};
    m_rfFrames[1] = {};
    m_scopeRunning = true;
    m_rfStage = RfStage::ArmingFollower;
    emit scopeStateChanged();
    setStatus(QStringLiteral("Funktrigger: Empfänger wird scharfgestellt"));
    m_rfTimeout.start(5000);
    auto *follower = m_scopeTriggerSource == 1 ? m_dso2 : m_dso;
    if (!follower->setSynchronizedSettings(m_rfSettings, m_scopeRfTriggerSample,
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
    if (!master->setSynchronizedSettings(m_rfSettings, m_scopeRfTriggerSample,
                                         DsoService::SyncRole::Master, m_rfGroup)) {
        failRfCapture(QStringLiteral("Funktrigger: Quelle konnte nicht scharfgestellt werden"));
    }
}

void PokitController::onRfMetadata(int channel, const DsoService::Metadata &metadata)
{
    if (m_rfStage != RfStage::WaitingForFrames || metadata.status != DsoService::DsoStatus::Done
        || metadata.mode != m_rfSettings.mode || metadata.samplingWindow != m_rfSettings.samplingWindow
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
    m_scopeRunning = false;
    updateScopeMetrics();
    updateScopeMetrics2();
    emit scopeStateChanged();
    emit scopeSamplesChanged();
    setStatus(QStringLiteral("Funktrigger CH%1 · Aufnahmepaar %2 · %3 Samples je Kanal")
              .arg(m_scopeTriggerSource).arg(*m_scopeMetadata.captureSequence).arg(m_scopeValues.size()));
    publishAutomationState();
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
    stopRfCapture();
    m_scopeRunning = false;
    setStatus(message);
    emit scopeStateChanged();
    publishAutomationState();
}

