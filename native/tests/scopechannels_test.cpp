#include "scopechannels.h"
#include <QCoreApplication>
#include <limits>
static void require(bool valid,const char *message){if(!valid)qFatal("%s",message);}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);ScopeChannels channels;
    require(channels.at(1).visible&&channels.at(2).visible&&!channels.at(3).visible&&!channels.at(4).visible,"Only implemented channels should start visible");
    require(channels.setScale(1,.1,.2),"CH1 scale change must succeed");
    require(channels.setScale(2,5,-2),"CH2 scale change must succeed");
    require(channels.at(1).voltsPerDiv==.1&&channels.at(1).offset==.2,"CH2 must not overwrite CH1");
    require(channels.at(1).peak()==.4&&channels.at(2).peak()==20,"Acquisition ranges must use the individual channel scale");
    require(std::abs(channels.at(1).voltageAt(.25)-.4)<1e-12,"Voltage cursors must respect channel scale and offset");
    require(channels.at(2).voltageAt(.5)==-2,"Center voltage must match the selected channel offset");
    channels.setVisible(1,false);channels.setVisible(2,false);
    require(!channels.at(1).visible&&!channels.at(2).visible,"All traces can be hidden");
    channels.setVisible(1,true);
    require(channels.at(1).visible&&!channels.at(2).visible&&channels.at(1).voltsPerDiv==.1,"Re-enabling must retain each channel's settings");
    require(!channels.setScale(0,1,0)&&!channels.setScale(5,1,0)&&!channels.setVisible(0,true),"Invalid channels must be rejected");
    require(!channels.setScale(1,-1,0)&&!channels.setScale(1,std::numeric_limits<double>::quiet_NaN(),0),"Invalid scales must be rejected");
    require(!channels.setScale(1,1,std::numeric_limits<double>::infinity()),"Invalid positions must be rejected");
    channels.setScale(2,.01,100);require(channels.at(2).offset==.04,"Offset must stay within four divisions");
    const auto state=channels.values();require(state.size()==4&&state[0].toMap()["voltsPerDiv"].toDouble()==.1,"Published state must preserve channel identities");
    require(channels.setCurrent(2,true)&&channels.at(2).unit()=="A","Channel mode must switch to amperes");
    require(channels.at(1).unit()=="V"&&channels.at(1).voltsPerDiv==.1,"Current mode must not alter the voltage channel");
    require(channels.at(2).offset==0&&channels.at(2).voltsPerDiv==.01,"Changing units must reset incompatible vertical settings");
    channels.setScale(2,.00001,.00002);require(channels.at(2).voltsPerDiv==.00001,"Microamp current scales must remain usable");
    require(ScopeChannels::currentRange(.0005,false)==0&&ScopeChannels::currentRange(.002,false)==1&&ScopeChannels::currentRange(.01,false)==2,"Select explicit small-current firmware ranges");
    require(ScopeChannels::currentRange(.125,false)==3&&ScopeChannels::currentRange(.3,false)==4,"Select low-current ranges up to 300 mA");
    require(ScopeChannels::currentRange(.301,false)==-1&&ScopeChannels::currentRange(11,true)==-1,"Reject peaks above the selected current path capacity");
    require(ScopeChannels::currentRange(1,true)==5&&ScopeChannels::currentRange(8,true)==6,"High-current path uses 3 A / 10 A firmware ranges");
    require(channels.setCurrent(2,false)&&channels.at(2).unit()=="V"&&channels.at(2).voltsPerDiv==1,"Switching back must restore voltage units and safe display defaults");
}
