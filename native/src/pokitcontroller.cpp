#include "pokitcontroller.h"

#include <qtpokit/abstractpokitservice.h>
#include <qtpokit/pokitdevice.h>
#include <qtpokit/pokitdiscoveryagent.h>
#include <qtpokit/pokitpro.h>

#include <QBluetoothDeviceInfo>
#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLowEnergyController>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

PokitController::PokitController(QObject *parent) : QObject(parent)
{
    m_discovery = new PokitDiscoveryAgent(this);
    m_discovery->setLowEnergyDiscoveryTimeout(6000);
    connect(m_discovery, &PokitDiscoveryAgent::pokitDeviceDiscovered,
            this, &PokitController::attachDevice);
    connect(m_discovery, &PokitDiscoveryAgent::finished, this, [this] {
        if (!m_device) {
            setStatus(QStringLiteral("Kein Pokit gefunden – neuer Suchlauf …"));
            QTimer::singleShot(3000, this, &PokitController::connectDevice);
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
    m_meterWatchdog.setSingleShot(true);
    m_meterWatchdog.setInterval(5000);
    connect(&m_meterWatchdog, &QTimer::timeout, this, [this] {
        if (!m_connected || !m_meterReady || m_feature != QStringLiteral("meter")) return;
        setStatus(QStringLiteral("Multimeterdaten fehlen – Kanal wird neu synchronisiert …"));
        m_meter->enableReadingNotifications();
        QTimer::singleShot(600, this, [this] {
            if (m_connected && m_feature == QStringLiteral("meter")) startMeter();
        });
    });
    m_automationTimer.setInterval(250);
    connect(&m_automationTimer, &QTimer::timeout,
            this, &PokitController::processAutomationCommands);
    m_automationTimer.start();
    publishAutomationState();
    QTimer::singleShot(500, this, &PokitController::connectDevice);
}

QVariantList PokitController::meterHistory() const
{
    QVariantList list; list.reserve(m_meterValues.size());
    for (double value : m_meterValues) list.append(value);
    return list;
}

QVariantList PokitController::scopeSamples() const
{
    QVariantList list; list.reserve(m_scopeValues.size());
    for (double value : m_scopeValues) list.append(value);
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
    if (m_connected) { activateCurrentFeature(); return; }
    if (m_device) { m_device->controller()->connectToDevice(); return; }
    if (m_discovery->isActive()) return;
    setStatus(QStringLiteral("Suche nach Pokit …"));
    m_discovery->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void PokitController::attachDevice(const QBluetoothDeviceInfo &info)
{
    if (m_device) return;
    m_discovery->stop();
    m_deviceName = info.name();
    emit deviceChanged();
    m_device = new PokitDevice(info, this);

    connect(m_device->controller(), &QLowEnergyController::connected, this, [this] {
        m_connected = true;
        emit connectedChanged();
        setStatus(QStringLiteral("Verbunden – Dienste werden initialisiert …"));
        QTimer::singleShot(400, this, &PokitController::activateCurrentFeature);
    });
    connect(m_device->controller(), &QLowEnergyController::disconnected, this, [this] {
        m_meterWatchdog.stop();
        m_scopeWatchdog.stop();
        m_scopeRestartTimer.stop();
        m_scopeStableFrames = 0;
        m_scopeRestartTimer.setInterval(500);
        m_connected = false;
        m_meterReady = false;
        m_dsoReady = false;
        emit connectedChanged();
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

void PokitController::ensureMeter()
{
    if (!m_device || m_meter) return;
    m_meter = m_device->multimeter();
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
        m_meterWatchdog.start();
        m_meterValue = reading.value;
        m_meterValues.append(reading.value);
        if (m_meterValues.size() > 7200) m_meterValues.remove(0, m_meterValues.size() - 7200);
        emit meterValueChanged(); emit meterHistoryChanged();
        setStatus(QStringLiteral("Live-Messung"));
        publishAutomationState();
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
                if (m_feature == QStringLiteral("scope") && m_connected) startScope();
            });
        });
    });
    connect(m_dso, &AbstractPokitService::serviceErrorOccurred, this,
            [](QLowEnergyService::ServiceError error) { qWarning() << "DSO service error" << int(error); });
    connect(m_dso, &DsoService::metadataRead, this, [this](const DsoService::Metadata &meta) {
        if (m_feature != QStringLiteral("scope")) return;
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
    QJsonObject scope{
        {QStringLiteral("sample_rate_hz"), double(m_scopeMetadata.samplingRate)},
        {QStringLiteral("sampling_window_us"), double(m_scopeMetadata.samplingWindow)},
        {QStringLiteral("samples"), m_scopeValues.size()},
        {QStringLiteral("time_per_div_us"), m_scopeTimePerDivUs},
        {QStringLiteral("volts_per_div"), m_scopeVoltsPerDiv},
        {QStringLiteral("coupling"), m_scopeCoupling},
        {QStringLiteral("trigger_mode"), m_triggerMode},
        {QStringLiteral("trigger_level_v"), m_triggerLevel},
        {QStringLiteral("metrics"), metrics},
        {QStringLiteral("waveform_v"), waveform}
    };
    QJsonObject state{
        {QStringLiteral("schema"), QStringLiteral("pokitsuite.telemetry.v1")},
        {QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("connected"), m_connected},
        {QStringLiteral("device"), m_deviceName},
        {QStringLiteral("status"), m_status},
        {QStringLiteral("active_feature"), m_feature},
        {QStringLiteral("multimeter"), QJsonObject{
            {QStringLiteral("value"), m_meterValue}, {QStringLiteral("unit"), m_meterUnit}}},
        {QStringLiteral("scope"), scope}
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

void PokitController::showMultimeter()
{
    if (m_feature == QStringLiteral("meter")) return;
    m_scopeAccepting = false;
    m_scopeWatchdog.stop();
    m_scopeRestartTimer.stop();
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
    m_meterWatchdog.stop();
    m_scopeAccepting = true;
    m_scopeRunning = true;
    m_scopeSingle = false;
    m_scopeValues.clear();
    m_scopePendingValues.clear();
    emit scopeSamplesChanged();
    emit scopeStateChanged();
    m_feature = QStringLiteral("scope"); emit activeFeatureChanged();
    activateCurrentFeature();
}

void PokitController::activateCurrentFeature()
{
    if (!m_connected) { connectDevice(); return; }
    if (m_feature == QStringLiteral("meter")) { ensureMeter(); startMeter(); }
    else { ensureDso(); startScope(); }
}

void PokitController::setMeterMode(const QString &mode)
{
    m_meterModeName = mode;
    if (mode.contains(QStringLiteral("Current"))) m_meterUnit = mode.startsWith(QStringLiteral("AC")) ? QStringLiteral("A AC") : QStringLiteral("A DC");
    else m_meterUnit = mode.startsWith(QStringLiteral("AC")) ? QStringLiteral("V AC") : QStringLiteral("V DC");
    emit meterValueChanged();
    if (m_feature == QStringLiteral("meter")) startMeter();
}

void PokitController::setMeterInterval(int milliseconds)
{
    m_meterInterval = qBound(250, milliseconds, 10000);
    if (m_feature == QStringLiteral("meter")) startMeter();
}

void PokitController::startMeter()
{
    if (!m_connected || !m_meterReady) return;
    MultimeterService::Mode mode = MultimeterService::Mode::DcVoltage;
    if (m_meterModeName == QStringLiteral("AC Voltage")) mode = MultimeterService::Mode::AcVoltage;
    else if (m_meterModeName == QStringLiteral("DC Current")) mode = MultimeterService::Mode::DcCurrent;
    else if (m_meterModeName == QStringLiteral("AC Current")) mode = MultimeterService::Mode::AcCurrent;
    m_meter->setSettings({mode, 255, quint32(m_meterInterval)});
    m_meterWatchdog.start();
    setStatus(QStringLiteral("Multimeter wird gestartet …"));
}

void PokitController::setScopeTrigger(const QString &mode, double level)
{
    m_triggerMode = mode; m_triggerLevel = level;
    m_scopeRestartTimer.stop();
    if (m_feature == QStringLiteral("scope") && m_scopeRunning) startScope();
}

void PokitController::setScopeConfig(const QString &coupling, double voltsPerDiv, int timePerDivUs)
{
    m_scopeCoupling = coupling;
    m_scopeVoltsPerDiv = qBound(0.01, voltsPerDiv, 100.0);
    m_scopeTimePerDivUs = qBound(10, timePerDivUs, 1000000);
    m_scopeRestartTimer.stop();
    if (m_feature == QStringLiteral("scope") && m_scopeRunning) startScope();
}

void PokitController::setScopeRunning(bool running)
{
    m_scopeRunning = running;
    m_scopeSingle = false;
    emit scopeStateChanged();
    if (!m_connected || !m_dsoReady) return;
    if (running) startScope();
    else {
        m_scopeWatchdog.stop();
        m_scopeRestartTimer.stop();
        m_scopeAccepting = false;
        m_dso->setSettings({DsoService::Command::FreeRunning, 0.0f, DsoService::Mode::Idle,
                            +PokitPro::VoltageRange::_10V, 100000, 1000});
        setStatus(QStringLiteral("Scope angehalten"));
    }
}

void PokitController::scopeSingle()
{
    m_scopeSingle = true;
    m_scopeRunning = true;
    emit scopeStateChanged();
    startScope();
}

void PokitController::startScope()
{
    if (!m_connected || !m_dsoReady || !m_scopeRunning) return;
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
    m_expectedScopeWindowUs = windowUs;
    m_expectedScopeSamples = samples;
    m_scopeAccepting = false;
    m_scopePendingValues.clear();
    m_dso->setSettings({command, safeTrigger, mode, range, windowUs, samples});
    m_scopeWatchdog.start();
    if (m_scopeValues.isEmpty())
        setStatus(QStringLiteral("Scope wird gestartet …"));
}

