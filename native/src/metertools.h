#pragma once
#include <QObject>
#include <QVariantMap>
#include <QFile>
#include <deque>

// Worker-thread DMM tools. CSV is append-only; UI snapshots are bounded.
class MeterTools final : public QObject {
    Q_OBJECT
public:
    explicit MeterTools(QObject *parent = nullptr, const QString &directory = {});
    QVariantMap state() const;
    void setUnit(int channel, const QString &unit);
    void sample(int channel, double value, qint64 timestamp = 0);
    void command(const QString &action, int channel, const QVariantMap &options = {});
signals:
    void changed();
private:
    struct Point { qint64 time; double value; };
    struct Channel {
        QString unit;
        quint64 count = 0;
        double minimum = 0, maximum = 0, mean = 0, last = 0;
        bool hold = false, relative = false, limits = false;
        double held = 0, reference = 0, lower = 0, upper = 1;
        int alarm = 0;
        std::deque<Point> points;
    };
    Channel m_channels[2];
    QString m_directory, m_message, m_csvPath, m_pdfPath;
    QFile m_log;
    QVariantList m_events;
    qint64 m_started = 0;
    quint64 m_rows = 0;
    double m_windowSeconds = 120, m_offsetSeconds = 0;
    qint64 m_viewEnd = 0;
    bool m_follow = true;
    void event(int channel, const QString &kind, const QString &text, qint64 time = 0);
    bool writeRow(const QStringList &columns);
    void exportPdf();
    QVariantMap channelState(int index, qint64 start, qint64 end) const;
};
