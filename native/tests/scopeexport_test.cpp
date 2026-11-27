#include "scopeexport.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <cmath>

static void require(bool valid,const char *message){if(!valid)qFatal("%s",message);}
static QJsonObject channel(int id) {
    const double scales[]={1,.02,2,.5},amplitudes[]={2.3,.05,3,1},offsets[]={.1,.02,-.2,0};
    const QStringList colors{"#ff6666","#d3d3d3","#ffd629","#55df89"};
    const int count=140+id*20;const double rate=count*500.0;
    QJsonArray wave;for(int i=0;i<count;++i)wave.append(amplitudes[id-1]*std::sin(2*3.141592653589793*1000*i/rate));
    return {{"channel",id},{"device",QString("Fixture Pokit %1").arg(id)},{"color",colors[id-1]},
        {"visible",id!=2},{"connected",true},{"waveform_v",wave},
        {"settings",QJsonObject{{"sample_rate_hz",rate},{"sampling_window_us",2000},{"time_per_div_us",200},
            {"volts_per_div",scales[id-1]},{"vertical_position_v",offsets[id-1]},
            {"coupling","DC"},{"trigger_mode","rising"},{"trigger_level_v",.01},{"trigger_sample",count/4}}},
        {"metrics",QJsonObject{{"minimum_v",-amplitudes[id-1]},{"maximum_v",amplitudes[id-1]},
            {"vpp_v",2*amplitudes[id-1]},{"mean_v",0},{"rms_v",amplitudes[id-1]/std::sqrt(2.0)},
            {"frequency_hz",1000},{"period_s",.001},{"cycles",2},{"duty_cycle_percent",50}}}};
}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);QTemporaryDir temporary;
    QDir directory(argc>1?QString::fromLocal8Bit(argv[1]):temporary.path());require(directory.mkpath("."),"Create export test directory");
    QJsonArray channels{channel(1),channel(2),channel(3),channel(4)};
    QJsonObject capture=channels[0].toObject();
    capture.insert("display_name","Multi-channel export test");capture.insert("timestamp_utc","2026-11-27T10:00:00Z");
    capture.insert("channels",channels);capture.insert("radio_paired",true);capture.insert("trigger_fraction",.25);capture.insert("view_window_us",2000);
    require(ScopeExport::channels(capture).size()==4,"Include all channels, including the hidden one");
    require(ScopeExport::writePdf(directory.filePath("four-channel.pdf"),capture),"Write four-channel PDF");
    require(ScopeExport::writeCsv(directory.filePath("four-channel.csv"),capture),"Write four-channel CSV");
    QFile csv(directory.filePath("four-channel.csv"));require(csv.open(QIODevice::ReadOnly),"Read CSV");
    const auto lines=csv.readAll().trimmed().split('\n');int counts[4]{};
    require(lines.first().trimmed()=="sample_index,time_s,value,unit,channel","CSV must identify the channel");
    for(int i=1;i<lines.size();++i){const auto columns=lines[i].trimmed().split(',');require(columns.size()==5,"Five CSV columns required");const int id=columns[4].toInt();require(id>=1&&id<=4,"Valid channel ID");++counts[id-1];}
    for(int i=0;i<4;++i)require(counts[i]==channels[i].toObject()["waveform_v"].toArray().size(),"CSV must retain every sample, including hidden channels");
    require(lines[1].split(',')[1].toDouble()<0,"Radio CSV time must be trigger-relative");
    capture.insert("channels",QJsonArray{channel(1),channel(2)});
    require(ScopeExport::writePdf(directory.filePath("two-channel.pdf"),capture),"Write two-channel overview and detail pages");
    capture.insert("channels",QJsonArray{channel(2)});
    require(ScopeExport::writePdf(directory.filePath("ch2-only.pdf"),capture),"CH2-only captures must not depend on CH1");
    auto legacy=channel(1);legacy.remove("channel");
    require(ScopeExport::channels(legacy).size()==1,"Old capture format remains supported");
    require(ScopeExport::writePdf(directory.filePath("legacy.pdf"),legacy),"Write legacy capture PDF");
    require(!ScopeExport::writePdf(directory.filePath("empty.pdf"),QJsonObject{}),"Reject empty report");
    auto invalid=channel(1);auto settings=invalid["settings"].toObject();settings.insert("sample_rate_hz",0);invalid.insert("settings",settings);
    require(!ScopeExport::writeCsv(directory.filePath("invalid.csv"),invalid),"Reject missing timebase");
    auto current=ScopeExport::normalize(channel(2),true);
    require(current.value("unit")=="A"&&!current.contains("waveform_v")&&current.contains("waveform_a"),"Current samples must not be labelled as volts");
    require(!current.value("metrics").toObject().contains("vpp_v")&&current.value("metrics").toObject().contains("vpp_a"),"Current metrics require ampere units");
    const auto telemetry=ScopeExport::normalize(QJsonObject{{"volts_per_div",.01},{"trigger_level_v",.002},{"waveform_v",QJsonArray{.001,.002}}},true);
    require(!telemetry.contains("volts_per_div")&&!telemetry.contains("trigger_level_v")&&telemetry.value("amps_per_div").toDouble()==.01,"Inline telemetry must not retain misleading voltage field names");
    capture.insert("channels",QJsonArray{channel(1),current});
    require(ScopeExport::writePdf(directory.filePath("voltage-current.pdf"),capture),"Mixed voltage/current report");
    require(ScopeExport::writeCsv(directory.filePath("voltage-current.csv"),capture),"Mixed voltage/current table");
    QFile mixed(directory.filePath("voltage-current.csv"));require(mixed.open(QIODevice::ReadOnly),"Read mixed CSV");require(mixed.readAll().contains(",A,2"),"Each current row must carry its A unit");
    QFile json(directory.filePath("fixture.json"));require(json.open(QIODevice::WriteOnly),"Write inspectable fixture");
    capture.insert("channels",channels);json.write(QJsonDocument(capture).toJson());
}
