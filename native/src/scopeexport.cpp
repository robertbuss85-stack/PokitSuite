#include "scopeexport.h"
#include <QPdfWriter>
#include <QPageSize>
#include <QPainter>
#include <QSaveFile>
#include <QTextStream>
#include <QFontMetrics>
#include <cmath>

QJsonArray ScopeExport::waveform(const QJsonObject &channel) {
    if(channel.contains("waveform"))return channel.value("waveform").toArray();
    return channel.value(channel.value("unit").toString()=="A"?"waveform_a":"waveform_v").toArray();
}

QJsonObject ScopeExport::normalize(QJsonObject channel,bool current) {
    const auto wave=waveform(channel);channel.insert("unit",current?"A":"V");channel.insert("quantity",current?"current":"voltage");
    channel.insert("waveform",wave);channel.insert(current?"waveform_a":"waveform_v",wave);
    if(current)channel.remove("waveform_v");
    auto metrics=channel.value("metrics").toObject();
    for(const QString key:{"minimum","maximum","vpp","mean","rms"}){
        const auto value=metrics.contains(key)?metrics.value(key):metrics.contains(key+(current?"_a":"_v"))?metrics.value(key+(current?"_a":"_v")):metrics.value(key+"_v");
        if(!value.isUndefined()){metrics.insert(key,value);metrics.insert(key+(current?"_a":"_v"),value);}
        if(current)metrics.remove(key+"_v");
    }
    channel.insert("metrics",metrics);auto settings=channel.value("settings").toObject();
    const bool inlineSettings=!channel.contains("settings");
    if(inlineSettings)for(const auto *key:{"volts_per_div","vertical_position_v","trigger_level_v","sample_rate_hz","sampling_window_us","time_per_div_us","coupling","trigger_mode"})
        if(channel.contains(key))settings.insert(key,channel.value(key));
    for(const auto &pair:QList<QPair<QString,QString>>{{"units_per_div","volts_per_div"},{"vertical_position","vertical_position_v"},{"trigger_level","trigger_level_v"}}){
        const auto value=settings.contains(pair.first)?settings.value(pair.first):settings.value(pair.second);settings.insert(pair.first,value);
        const QString ampKey=pair.first=="units_per_div"?"amps_per_div":pair.first=="vertical_position"?"vertical_position_a":"trigger_level_a";
        if(current){settings.remove(pair.second);settings.insert(ampKey,value);}
        if(inlineSettings){channel.insert(pair.first,value);if(current){channel.remove(pair.second);channel.insert(ampKey,value);}}
    }
    channel.insert("settings",settings);return channel;
}

namespace {
QString num(double value) { return QString::number(value,'g',7); }
QColor color(const QJsonObject &channel) {
    const QColor result(channel.value("color").toString());
    return result.isValid()?result:QColor("#ffd629");
}

QString name(const QJsonObject &channel) { return "CH"+QString::number(channel.value("channel").toInt(1)); }
bool valid(const QJsonArray &channels) {
    if(channels.isEmpty()||channels.size()>4)return false;
    for(const auto &value:channels) {
        const auto c=value.toObject(),s=c.value("settings").toObject();
        if(!(s.value("sample_rate_hz").toDouble()>0)||!(s.value("units_per_div").toDouble()>0))return false;
        for(const auto &sample:ScopeExport::waveform(c))if(!sample.isDouble()||!std::isfinite(sample.toDouble()))return false;
    }
    return true;
}
void text(QPainter &p,const QRectF &rect,const QString &value,int size=10,bool bold=false,const QColor &ink=QColor("#213747")) {
    p.setFont(QFont("Segoe UI",size,bold?QFont::Bold:QFont::Normal));p.setPen(ink);
    p.drawText(rect,Qt::AlignLeft|Qt::AlignVCenter,value);
}
void plot(QPainter &p,const QRectF &area,const QJsonArray &channels,double windowUs,double triggerFraction,bool radio) {
    p.fillRect(area,QColor("#07100b"));p.setPen(QPen(QColor("#294a34"),.8));
    for(int x=0;x<=10;++x)p.drawLine(QPointF(area.left()+x*area.width()/10,area.top()),QPointF(area.left()+x*area.width()/10,area.bottom()));
    for(int y=0;y<=8;++y)p.drawLine(QPointF(area.left(),area.top()+y*area.height()/8),QPointF(area.right(),area.top()+y*area.height()/8));
    p.save();p.setClipRect(area.adjusted(1,1,-1,-1));p.setRenderHint(QPainter::Antialiasing);
    for(const auto &value:channels) {
        const auto channel=value.toObject(),settings=channel.value("settings").toObject();
        const auto waveform=ScopeExport::waveform(channel);
        const double scale=settings.value("units_per_div").toDouble(1),offset=settings.value("vertical_position").toDouble();
        const double rate=settings.value("sample_rate_hz").toDouble();
        const double trigger=radio?settings.value("trigger_sample").toDouble():0;
        const double origin=radio?triggerFraction*area.width():0;
        p.setPen(QPen(color(channel),1.4));QPointF previous;
        for(int i=0;i<waveform.size();++i) {
            const QPointF point(area.left()+origin+(i-trigger)*area.width()*1e6/(rate*windowUs),
                area.center().y()-(waveform[i].toDouble()-offset)/scale*area.height()/8);
            if(i)p.drawLine(previous,point);previous=point;
        }
    }
    p.restore();
    text(p,QRectF(area.left()+8,area.bottom()-28,area.width()-16,24),num(windowUs/10)+" us/div",9,false,QColor("#d4e2da"));
}
double window(const QJsonObject &capture) {
    const auto settings=capture.value("settings").toObject();
    return qMax(1.0,capture.value("view_window_us").toDouble(settings.value("time_per_div_us").toDouble(10000)*10));
}
}

QJsonArray ScopeExport::channels(const QJsonObject &capture) {
    QJsonArray result;
    for(const auto &value:capture.value("channels").toArray()) {
        const auto channel=value.toObject();
        if(ScopeExport::waveform(channel).size()>1)result.append(normalize(channel,channel.value("unit").toString()=="A"));
    }
    if(result.isEmpty()&&ScopeExport::waveform(capture).size()>1) {
        auto legacy=capture;legacy.remove("channels");
        if(!legacy.contains("channel"))legacy.insert("channel",1);
        result.append(normalize(legacy,legacy.value("unit").toString()=="A"));
    }
    return result;
}

bool ScopeExport::writeCsv(const QString &path,const QJsonObject &capture) {
    const auto all=channels(capture);if(!valid(all))return false;
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text))return false;
    QTextStream out(&file);out<<"sample_index,time_s,value,unit,channel\n";
    for(const auto &value:all) {
        const auto channel=value.toObject(),settings=channel.value("settings").toObject();
        const auto waveform=ScopeExport::waveform(channel);
        const double rate=settings.value("sample_rate_hz").toDouble();
        const double trigger=capture.value("radio_paired").toBool()?settings.value("trigger_sample").toDouble():0;
        for(int i=0;i<waveform.size();++i)out<<i<<','<<QString::number((i-trigger)/rate,'g',12)<<','
            <<QString::number(waveform[i].toDouble(),'g',12)<<','<<channel.value("unit").toString("V")<<','<<channel.value("channel").toInt(1)<<'\n';
    }
    out.flush();return out.status()==QTextStream::Ok&&file.commit();
}

bool ScopeExport::writePdf(const QString &path,const QJsonObject &capture) {
    const auto all=channels(capture);if(!valid(all))return false;
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    {
        QPdfWriter pdf(&file);pdf.setPageSize(QPageSize(QPageSize::A4));pdf.setResolution(120);
        pdf.setTitle("PokitSuite Oscilloscope Report");pdf.setCreator("PokitSuite");
        QPainter p(&pdf);if(!p.isActive())return false;
        const double w=pdf.width(),h=pdf.height(),win=window(capture);
        const bool radio=capture.value("radio_paired").toBool();
        const double triggerFraction=capture.value("trigger_fraction").toDouble();
        const int pages=all.size()+1;
        auto header=[&](const QString &title,int page) {
            p.fillRect(QRectF(0,0,w,h),Qt::white);
            text(p,QRectF(30,15,w-60,55),title,19,true);
            text(p,QRectF(30,72,w-60,30),"Capture: "+capture.value("display_name").toString(),10);
            text(p,QRectF(30,h-42,w-60,25),QString("PokitSuite | %1 | Page %2 / %3").arg(capture.value("timestamp_utc").toString()).arg(page).arg(pages),8,false,QColor("#637a89"));
        };
        header("PokitSuite | Oscilloscope overview",1);
        text(p,QRectF(30,106,w-60,28),radio?"Radio-triggered acquisition | time relative to trigger":"Independent acquisitions | no guaranteed inter-channel time alignment",10);
        for(int i=0;i<all.size();++i) {
            const auto c=all[i].toObject(),s=c.value("settings").toObject();
            const double x=30+(i%2)*(w-60)/2,y=148+(i/2)*36;
            p.fillRect(QRectF(x,y+9,15,15),color(c));
            text(p,QRectF(x+23,y,(w-60)/2-23,32),name(c)+"  "+num(s.value("units_per_div").toDouble())+" "+c.value("unit").toString("V")+"/div | offset "+num(s.value("vertical_position").toDouble())+" "+c.value("unit").toString("V"),9,true);
        }
        plot(p,QRectF(30,242,w-60,450),all,win,triggerFraction,radio);
        text(p,QRectF(30,713,w-60,35),"Channel measurements",13,true);
        const double widths[]={.12,.21,.21,.22,.24};
        const QStringList headings{"Channel / unit","Peak-to-peak","RMS","Frequency [Hz]","Period [s]"};
        double x=30;
        for(int j=0;j<5;++j){text(p,QRectF(x+5,758,(w-60)*widths[j]-10,36),headings[j],9,true);x+=(w-60)*widths[j];}
        for(int i=0;i<all.size();++i) {
            const auto c=all[i].toObject(),m=c.value("metrics").toObject();const double y=800+i*46;
            p.fillRect(QRectF(30,y,w-60,44),QColor(i%2?"#edf3f6":"#f7fafb"));
            const QStringList values{name(c)+" ["+c.value("unit").toString("V")+"]",num(m.value("vpp").toDouble()),num(m.value("rms").toDouble()),
                m.value("frequency_hz").toDouble()>0?num(m.value("frequency_hz").toDouble()):"-",
                m.value("period_s").toDouble()>0?num(m.value("period_s").toDouble()):"-"};
            x=30;for(int j=0;j<5;++j){text(p,QRectF(x+5,y,(w-60)*widths[j]-10,44),values[j],10);x+=(w-60)*widths[j];}
        }
        p.setFont(QFont("Segoe UI",9));p.setPen(QColor("#526b7b"));
        p.drawText(QRectF(30,1025,w-60,100),Qt::TextWordWrap,
            "All channels with complete stored waveforms are included, even if hidden on screen. Each trace uses its own amplitude unit, scale and offset. See channel detail pages for device, settings and visibility status. Raw values for every channel are available in the companion CSV and JSON files.");
        for(int i=0;i<all.size();++i) {
            if(!pdf.newPage())return false;
            const auto c=all[i].toObject(),s=c.value("settings").toObject(),m=c.value("metrics").toObject();
            header("PokitSuite | "+name(c)+" detail",i+2);
            text(p,QRectF(30,108,w-60,30),"Device: "+c.value("device").toString(),11,true);
            text(p,QRectF(30,145,w-60,30),QString("%1 | %2").arg(c.value("visible").toBool(true)?"Visible on screen":"Hidden on screen; included in report",c.value("connected").toBool(true)?"Device connected":"Device offline; last stored complete frame"),9);
            plot(p,QRectF(30,195,w-60,415),QJsonArray{c},win,triggerFraction,radio);
            struct Row{QString label,value;};
            const QList<Row> rows{
                {"Minimum",num(m.value("minimum").toDouble())+" "+c.value("unit").toString("V")},{"Maximum",num(m.value("maximum").toDouble())+" "+c.value("unit").toString("V")},
                {"Peak-to-peak",num(m.value("vpp").toDouble())+" "+c.value("unit").toString("V")},{"Mean",num(m.value("mean").toDouble())+" "+c.value("unit").toString("V")},
                {"RMS",num(m.value("rms").toDouble())+" "+c.value("unit").toString("V")},{"Frequency",m.value("frequency_hz").toDouble()>0?num(m.value("frequency_hz").toDouble())+" Hz":"-"},
                {"Period",m.value("period_s").toDouble()>0?num(m.value("period_s").toDouble())+" s":"-"},
                {"Cycles",num(m.value("cycles").toDouble())},{"Duty cycle",num(m.value("duty_cycle_percent").toDouble())+" %"},
                {"Sample rate / count",num(s.value("sample_rate_hz").toDouble())+" S/s | "+QString::number(ScopeExport::waveform(c).size())+" samples"},
                {"Time / div",num(win/10)+" us"},{"Amplitude / div",num(s.value("units_per_div").toDouble())+" "+c.value("unit").toString("V")},
                {"Vertical position",num(s.value("vertical_position").toDouble())+" "+c.value("unit").toString("V")},{"Coupling",s.value("coupling").toString()},
                {"Trigger",radio?"Radio CH"+QString::number(capture.value("trigger_source").toInt(1))+" | "+num(capture.value("trigger_source_level").toDouble())+" "+capture.value("trigger_unit").toString("V")
                    :s.value("trigger_mode").toString()+" | "+num(s.value("trigger_level").toDouble())+" "+c.value("unit").toString("V")}
            };
            for(int row=0;row<rows.size();++row) {
                const double y=645+row*34;p.fillRect(QRectF(30,y,w-60,33),QColor(row%2?"#edf3f6":"#f7fafb"));
                text(p,QRectF(40,y,(w-80)*.44,33),rows[row].label,10);
                text(p,QRectF(40+(w-80)*.45,y,(w-80)*.55,33),rows[row].value,10);
            }
        }
        p.end();
    }
    return file.commit();
}
