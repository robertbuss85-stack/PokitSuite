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
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
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
    refreshSavedCaptures();
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

