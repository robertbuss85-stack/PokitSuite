#include "metertools.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QPdfWriter>
#include <QPainter>
#include <QPageSize>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace {
QString number(double value) { return QString::number(value, 'g', 12); }
QString iso(qint64 time) { return QDateTime::fromMSecsSinceEpoch(time, Qt::UTC).toString(Qt::ISODateWithMs); }
QString csv(QString value) {
    value.replace('"', QStringLiteral("\"\""));
    return '"' + value + '"';
}
}

MeterTools::MeterTools(QObject *parent, const QString &directory) : QObject(parent),
    m_directory(directory.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("PokitSuite/Multimeter")) : directory) {}

bool MeterTools::writeRow(const QStringList &columns) {
    if (!m_log.isOpen()) return false;
    QStringList quoted;
    for (const auto &column : columns) quoted << csv(column);
    const auto bytes = (quoted.join(',') + '\n').toUtf8();
    if (m_log.write(bytes) != bytes.size() || !m_log.flush()) {
        m_message = QStringLiteral("Schreibfehler: Aufzeichnung gestoppt. Freien Speicher prüfen.");
        m_log.close();
        return false;
    }
    return true;
}

void MeterTools::event(int channel, const QString &kind, const QString &text, qint64 time) {
    if (!time) time = QDateTime::currentMSecsSinceEpoch();
    m_events.prepend(QVariantMap{{"time", iso(time)}, {"t", double(time)}, {"channel", channel}, {"kind", kind}, {"text", text}});
    if (m_events.size() > 100) m_events.removeLast();
    writeRow({iso(time), number((time-m_started)/1000.0), QString::number(channel), {}, {}, kind, text});
}

void MeterTools::setUnit(int channel, const QString &unit) {
    if (channel < 1 || channel > 2) return;
    auto &c = m_channels[channel-1];
    if (c.unit == unit) return;
    c = Channel{};
    c.unit = unit;
    event(channel, "mode", QStringLiteral("Messgröße gewechselt: %1; Statistik, REL, HOLD und Grenzen zurückgesetzt").arg(unit));
    emit changed();
}

void MeterTools::sample(int channel, double value, qint64 timestamp) {
    if (channel < 1 || channel > 2 || !std::isfinite(value)) return;
    if (!timestamp) timestamp = QDateTime::currentMSecsSinceEpoch();
    auto &c = m_channels[channel-1];
    if (!c.count) c.minimum = c.maximum = value;
    ++c.count;
    c.last = value;
    c.minimum = std::min(c.minimum, value);
    c.maximum = std::max(c.maximum, value);
    c.mean += (value-c.mean)/double(c.count);
    c.points.push_back({timestamp, value});
    if (c.points.size() > 100000) c.points.pop_front();
    const int alarm = c.limits ? (value<c.lower ? -1 : value>c.upper ? 1 : 0) : 0;
    if (alarm != c.alarm) {
        c.alarm = alarm;
        event(channel, "limit", alarm ? QStringLiteral("%1: %2 %3").arg(alarm<0?"Untergrenze unterschritten":"Obergrenze überschritten",number(value),c.unit)
                                      : QStringLiteral("Wieder im Grenzbereich"), timestamp);
    }
    if (m_log.isOpen() && writeRow({iso(timestamp),number((timestamp-m_started)/1000.0),QString::number(channel),number(value),c.unit,"sample",{}})) ++m_rows;
    emit changed();
}

QVariantMap MeterTools::channelState(int index, qint64 start, qint64 end) const {
    const auto &c = m_channels[index];
    QVariantList plot;
    auto first = std::lower_bound(c.points.begin(),c.points.end(),start,[](const Point &p,qint64 t){return p.time<t;});
    auto last = std::upper_bound(first,c.points.end(),end,[](qint64 t,const Point &p){return t<p.time;});
    const auto size = std::distance(first,last);
    const auto stride = std::max<qint64>(1,(size+599)/600);
    // Preserve both extrema in each bucket, in chronological order.
    for (auto it=first;it!=last;) {
        auto stop=it+std::min<qint64>(stride,std::distance(it,last));
        auto low=it, high=it;
        for(auto p=it;p!=stop;++p) {if(p->value<low->value)low=p;if(p->value>high->value)high=p;}
        if(high<low)std::swap(low,high);
        plot.append(QVariantMap{{"t",double(low->time)},{"v",low->value}});
        if(high!=low)plot.append(QVariantMap{{"t",double(high->time)},{"v",high->value}});
        it=stop;
    }
    return {{"unit",c.unit},{"count",double(c.count)},{"minimum",c.minimum},{"maximum",c.maximum},{"mean",c.mean},
        {"hold",c.hold},{"relative",c.relative},{"reference",c.reference},{"display",(c.hold?c.held:c.last)-(c.relative?c.reference:0)},
        {"limits",c.limits},{"lower",c.lower},{"upper",c.upper},{"alarm",c.alarm},{"plot",plot},
        {"oldest",c.points.empty()?0.0:double(c.points.front().time)}};
}

QVariantMap MeterTools::state() const {
    const auto end=(m_follow?QDateTime::currentMSecsSinceEpoch():m_viewEnd)-qint64(m_offsetSeconds*1000);
    const auto start=end-qint64(m_windowSeconds*1000);
    return {{"channels",QVariantList{channelState(0,start,end),channelState(1,start,end)}},
        {"recording",m_log.isOpen()},{"rows",double(m_rows)},{"csvPath",m_csvPath},{"pdfPath",m_pdfPath},
        {"message",m_message},{"events",m_events},{"window",m_windowSeconds},{"offset",m_offsetSeconds},
        {"follow",m_follow},{"start",double(start)},{"end",double(end)}};
}

void MeterTools::command(const QString &action,int channel,const QVariantMap &options) {
    if (action=="record") {
        if(m_log.isOpen()) {event(0,"stop",QStringLiteral("Aufzeichnung beendet"));if(m_log.isOpen())m_message="CSV-Aufzeichnung gespeichert";m_log.close();}
        else {
            if(!QDir().mkpath(m_directory)) {m_message="Aufzeichnungsordner kann nicht erstellt werden";emit changed();return;}
            m_started=QDateTime::currentMSecsSinceEpoch();m_rows=0;
            m_csvPath=QDir(m_directory).filePath("PokitMeter_"+QDateTime::fromMSecsSinceEpoch(m_started).toString("yyyyMMdd_HHmmss_zzz")+".csv");
            m_log.setFileName(m_csvPath);
            if(!m_log.open(QIODevice::WriteOnly|QIODevice::NewOnly))m_message="CSV-Aufzeichnung kann nicht geöffnet werden";
            else if(writeRow({"timestamp_utc","elapsed_s","channel","value","unit","kind","text"})) {
                event(0,"start","CSV recording started; values are absolute, independent of HOLD/REL");
                for(int i=0;i<2;++i) {const auto &c=m_channels[i];event(i+1,"configuration",QString("Unit: %1; limits: %2; lower: %3; upper: %4").arg(c.unit,c.limits?"on":"off",number(c.lower),number(c.upper)));}
                if(m_log.isOpen())m_message="Aufzeichnung läuft";
            }
        }
    } else if(action=="comment") {
        const auto text=options.value("text").toString().trimmed().left(1000);
        if(!text.isEmpty())event(0,"comment",text);
    } else if(action=="view") {
        const double window=options.value("seconds",m_windowSeconds).toDouble();
        const double offset=options.value("offset",m_offsetSeconds).toDouble();
        if(std::isfinite(window)&&std::isfinite(offset)) {
            m_windowSeconds=std::clamp(window,10.0,86400.0);
            if(options.contains("follow")) {
                const bool follow=options.value("follow").toBool();
                if(m_follow&&!follow)m_viewEnd=QDateTime::currentMSecsSinceEpoch();
                m_follow=follow;
            }
            m_offsetSeconds=std::clamp(offset,0.0,86400.0);
            if(m_follow)m_offsetSeconds=0;
        }
    } else if(action=="pdf") exportPdf();
    else if(action=="folder") {QDir().mkpath(m_directory);QDesktopServices::openUrl(QUrl::fromLocalFile(m_directory));}
    else if(channel>=1&&channel<=2) {
        auto &c=m_channels[channel-1];
        if(action=="hold"&&c.count) {c.hold=!c.hold;if(c.hold)c.held=c.last;}
        else if(action=="relative"&&c.count&&c.unit!="Durchgang") {c.relative=!c.relative;if(c.relative)c.reference=c.hold?c.held:c.last;}
        else if(action=="reset") {c.count=0;c.minimum=c.maximum=c.mean=0;c.hold=c.relative=false;event(channel,"reset","Statistik zurückgesetzt");}
        else if(action=="limits") {
            bool okLow=false,okHigh=false;
            const double lower=options.value("lower").toDouble(&okLow),upper=options.value("upper").toDouble(&okHigh);
            const bool enabled=options.value("enabled").toBool();
            if(enabled&&(!okLow||!okHigh||!std::isfinite(lower)||!std::isfinite(upper)||lower>=upper))m_message="Grenzen ungültig: Untergrenze muss kleiner als Obergrenze sein";
            else {c.limits=enabled;c.lower=lower;c.upper=upper;c.alarm=0;event(channel,"limits",enabled?QStringLiteral("Grenzen: %1 bis %2 %3").arg(number(lower),number(upper),c.unit):QStringLiteral("Grenzwertalarm aus"));m_message="Grenzwerte übernommen";}
        }
    }
    emit changed();
}

void MeterTools::exportPdf() {
    if(!QDir().mkpath(m_directory)){m_message="Berichtsordner kann nicht erstellt werden";return;}
    const QString path=QDir(m_directory).filePath("PokitMeter_Report_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+".pdf");
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)){m_message="PDF kann nicht angelegt werden";return;}
    {
        QPdfWriter pdf(&file);pdf.setPageSize(QPageSize(QPageSize::A4));pdf.setResolution(120);pdf.setTitle("PokitSuite multimeter report");
        QPainter p(&pdf);
        if(!p.isActive()){m_message="PDF-Erstellung fehlgeschlagen";return;}
        const int w=p.viewport().width(),h=p.viewport().height();
        p.fillRect(0,0,w,h,QColor("#ffffff"));p.setPen(QColor("#142735"));p.setFont(QFont("Segoe UI",20,QFont::Bold));
        p.drawText(QRect(30,20,w-60,50),"PokitSuite | Multimeter");p.setFont(QFont("Segoe UI",10));
        p.drawText(QRect(30,80,w-60,40),QDateTime::currentDateTime().toString(Qt::ISODate));
        p.drawText(QRect(30,120,w-60,55),Qt::TextWordWrap,"Absolute readings. HOLD/REL affect the display only. Statistics since last reset/mode change. CSV contains all recorded readings and events.");
        const auto view=state();const auto channels=view.value("channels").toList();
        for(int i=0;i<2;++i) {
            const auto c=channels[i].toMap();const int y=200+i*340;
            p.setFont(QFont("Segoe UI",13,QFont::Bold));p.drawText(30,y,w-60,30,0,QString("CH%1 | %2 | n = %3").arg(i+1).arg(c.value("unit").toString()).arg(c.value("count").toULongLong()));
            p.setFont(QFont("Segoe UI",11));
            p.drawText(30,y+38,w-60,30,0,c.value("count").toDouble()>0?QString("Min: %1   Max: %2   Mean: %3").arg(number(c.value("minimum").toDouble()),number(c.value("maximum").toDouble()),number(c.value("mean").toDouble())):QString("No valid readings"));
            const QRectF area(80,y+95,w-130,170);p.setPen(QColor("#ccd4da"));p.drawRect(area);
            const auto points=c.value("plot").toList();
            if(points.size()>1) {
                double low=points[0].toMap().value("v").toDouble(),high=low;
                for(const auto &point:points){const double v=point.toMap().value("v").toDouble();low=std::min(low,v);high=std::max(high,v);}
                if(low==high){low-=.5;high+=.5;}
                p.setPen(QColor("#526676"));p.drawText(30,y+95,number(high));p.drawText(30,y+265,number(low));
                p.setPen(QPen(i?QColor("#15759a"):QColor("#bb3535"),1.5));
                QPointF previous;double previousTime=0;
                for(const auto &point:points){const auto v=point.toMap();const double t=v.value("t").toDouble();
                    QPointF here(area.left()+(t-view.value("start").toDouble())/(view.value("end").toDouble()-view.value("start").toDouble())*area.width(),area.bottom()-(v.value("v").toDouble()-low)/(high-low)*area.height());
                    if(previousTime&&t-previousTime<5000)p.drawLine(previous,here);previous=here;previousTime=t;
                }
            }
            p.setPen(QColor("#526676"));p.drawText(80,y+290,QString("Time window: %1 s | %2").arg(view.value("window").toDouble()).arg(iso(qint64(view.value("end").toDouble()))));
        }
        p.setPen(QColor("#142735"));p.setFont(QFont("Segoe UI",12,QFont::Bold));p.drawText(30,920,"Recent events / comments");p.setFont(QFont("Segoe UI",9));
        int y=945;
        for(const auto &entry:m_events){const auto e=entry.toMap();const auto text=e.value("time").toString()+"  CH"+e.value("channel").toString()+"  "+e.value("text").toString();
            const QRect r(30,y,w-60,42);p.drawText(r,Qt::TextWordWrap,text.left(180));y+=48;if(y>h-65)break;}
        p.end();
    }
    if(file.commit()){m_pdfPath=path;m_message="PDF-Bericht gespeichert";}else m_message="PDF konnte nicht gespeichert werden";
}
