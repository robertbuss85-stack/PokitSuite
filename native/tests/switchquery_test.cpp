#include "switchquery.h"
#include "statusservice_p.h"
#include <QCoreApplication>
QTPOKIT_USE_NAMESPACE
static void require(bool ok,const char *message){if(!ok)qFatal("%s",message);}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    SwitchQuery q;
    require(q.begin(0)&&!q.begin(1),"Only one switch read may be pending");
    auto old=q.token;
    q.reset();require(q.begin(2),"Reconnect must permit a fresh read");
    require(!q.fail(old,3,false,"late"),"Old timeout must not cancel a new connection read");
    require(q.fail(q.token,4,false,"timeout")&&q.uncertain,"Unknown switch must fail closed");
    require(!q.begin(500)&&q.begin(1004),"Read retries must respect backoff");
    q.notifications=true;q.confirm(1100);
    require(!q.begin(31099)&&q.begin(31100),"Notifications reduce polling to every 30 seconds");
    q.confirm(31101,true);
    require(q.pending&&q.notifiedAt>q.requestedAt,"Notification must not release an outstanding GATT read");
    require(q.readSuperseded(),"Newer notification must supersede the pending read");
    q.pending=false;require(q.readSuperseded(),"A timed-out late read must not overwrite the notification either");
    q.confirm(31102);
    for(int i=1;i<=3;++i){
        require(q.begin(q.nextAttempt),"Retry must become available");
        require(q.fail(q.token,q.nextAttempt,true,"timeout"),"Current timeout must be handled");
        require(q.uncertain==(i==3),"Confirmed subscription tolerates two transient failures only");
    }
    q.confirm(50000);require(!q.uncertain&&q.failures==0&&q.error.isEmpty(),"Fresh status must clear failure state");
    q.reset();q.begin(0);q.confirm(1);q.begin(5001);
    q.fail(q.token,5002,true,"timeout");require(q.uncertain,"Without notifications a failed read must block current");
    q.reset();q.begin(0);q.confirm(0,true);
    require(q.readSuperseded(),"Notification precedence must also work within the same millisecond");
    StatusService status(nullptr);StatusServicePrivate parser(nullptr,&status);
    int legacy=0,updates=0;bool notification=false;
    QObject::connect(&status,&StatusService::deviceStatusRead,[&](const auto &){++legacy;});
    QObject::connect(&status,&StatusService::deviceStatusUpdate,[&](const auto &reading,bool pushed){
        require(reading.switchPosition.has_value(),"Valid status must contain a switch");++updates;notification=pushed;
    });
    QByteArray raw(8,'\0');
    for(int position=0;position<=2;++position){raw[6]=char(position);
        require(int(*StatusServicePrivate::parseStatus(raw).switchPosition)==position,"Switch byte must decode exactly");}
    parser.processStatusUpdate(raw,true);require(legacy==1&&updates==1&&notification,"Notification must reach the controller signal");
    parser.processStatusUpdate(raw,false);require(legacy==2&&updates==2&&!notification,"Explicit reads must retain their origin");
    raw[6]=char(255);require(!StatusServicePrivate::parseStatus(raw).switchPosition,"Invalid enum bytes must be rejected");
    require(!StatusServicePrivate::parseStatus(raw.left(6)).switchPosition,"Truncated packet must not fabricate a switch");
    return 0;
}
