#include "metertools.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <limits>
#include <cmath>

static void require(bool ok,const char *message) {if(!ok)qFatal("%s",message);}
static QVariantMap channel(const MeterTools &tools,int index=0) {return tools.state().value("channels").toList()[index].toMap();}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);QTemporaryDir temporary;
    const QString directory=argc>1?QString::fromLocal8Bit(argv[1]):temporary.path();
    MeterTools tools(nullptr,directory);const auto now=QDateTime::currentMSecsSinceEpoch();
    tools.setUnit(1,"V DC");tools.setUnit(2,"A DC");
    tools.sample(1,1,now-3000);tools.sample(1,3,now-2000);
    require(channel(tools)["mean"].toDouble()==2,"mean must use actual valid readings");
    tools.command("hold",1);tools.sample(1,5,now-1000);
    require(channel(tools)["display"].toDouble()==3&&channel(tools)["count"].toInt()==3,"HOLD must only freeze display");
    tools.command("relative",1);tools.sample(1,7,now);
    require(channel(tools)["display"].toDouble()==0,"REL should capture held absolute reference");
    tools.command("hold",1);
    require(channel(tools)["display"].toDouble()==4&&channel(tools)["mean"].toDouble()==4,"REL must not contaminate statistics");
    tools.command("limits",1,{{"enabled",true},{"lower",2},{"upper",6}});
    tools.sample(1,7);const int events=tools.state()["events"].toList().size();tools.sample(1,8);
    require(channel(tools)["alarm"].toInt()==1&&tools.state()["events"].toList().size()==events,"Alarm must log transitions only");
    tools.sample(1,4);require(channel(tools)["alarm"].toInt()==0,"Alarm should recover inside limits");
    tools.command("limits",1,{{"enabled",true},{"lower",10},{"upper",2}});
    require(channel(tools)["upper"].toDouble()==6,"Invalid limits must preserve previous configuration");
    tools.setUnit(1,"A AC");
    require(channel(tools)["count"].toInt()==0&&!channel(tools)["hold"].toBool()&&!channel(tools)["relative"].toBool()&&!channel(tools)["limits"].toBool(),"Unit change must clear incompatible state");
    tools.sample(1,std::numeric_limits<double>::quiet_NaN());
    require(channel(tools)["count"].toInt()==0,"Nonfinite values must not enter statistics");
    tools.setUnit(1,"V DC");
    tools.command("record",0);
    require(tools.state()["recording"].toBool(),"CSV recording must start");
    for(int i=0;i<100;++i){tools.sample(1,std::sin(i*.1)*3,now-100000+i*1000);tools.sample(2,.001*i,now-100000+i*1000);}
    tools.command("hold",1);tools.sample(1,3.2);tools.command("comment",0,{{"text","test, quoted \"marker\"\nsecond line"}});
    tools.command("record",0);
    QFile csv(tools.state()["csvPath"].toString());require(csv.open(QIODevice::ReadOnly),"CSV must be accessible after stop");
    const auto bytes=csv.readAll();require(bytes.contains("\"3.2\"")&&bytes.contains("quoted \"\"marker\"\""),"CSV must preserve raw samples and escape annotations");
    require(tools.state()["rows"].toInt()==201,"Recording count must include both channels and HOLD samples");
    tools.command("view",0,{{"seconds",120}});tools.command("pdf",0);
    QFile pdf(tools.state()["pdfPath"].toString());require(pdf.open(QIODevice::ReadOnly)&&pdf.read(4)=="%PDF","PDF export must succeed");
    tools.command("view",0,{{"seconds",10},{"follow",false},{"offset",30}});
    const auto end=tools.state()["end"].toDouble();
    tools.sample(1,8);require(tools.state()["end"].toDouble()==end,"Historical view must stay fixed while collecting new data");
    tools.command("view",0,{{"seconds",86400},{"follow",true}});
    for(int i=0;i<100100;++i)tools.sample(2,double(i%23),now-100100+i);
    const auto points=channel(tools,1)["plot"].toList();
    require(points.size()<=1200,"Long-running UI snapshots must stay bounded");
    require(channel(tools,1)["count"].toDouble()==100200,"Long-run statistics must include samples evicted from plot buffer");
    qInfo()<<"Meter tools OK: statistics, HOLD/REL, mode reset, alarms, CSV, PDF, zoom and bounded history";
    qInfo()<<"Artifacts:"<<directory;
}
