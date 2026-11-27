#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace ScopeExport {
QJsonArray waveform(const QJsonObject &channel);
QJsonObject normalize(QJsonObject channel, bool current);
// Optional multi-channel extension; original v1 single-channel files remain readable.
QJsonArray channels(const QJsonObject &capture);
bool writePdf(const QString &path, const QJsonObject &capture);
bool writeCsv(const QString &path, const QJsonObject &capture);
}
