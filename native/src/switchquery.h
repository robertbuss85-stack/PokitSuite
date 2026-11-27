#pragma once
#include <QString>
#include <QtGlobal>
#include <algorithm>

// Monotonic, tokenized status requests. A late timeout cannot cancel a newer read.
struct SwitchQuery {
    bool pending=false, subscribing=false, subscriptionAttempted=false;
    bool canNotify=false, notifications=false, uncertain=false;
    int failures=0, reads=0, updates=0;
    qint64 confirmedAt=-1, nextAttempt=0, requestedAt=-1, notifiedAt=-1;
    quint64 token=0;
    quint64 notificationRevision=0, requestNotificationRevision=0;
    QString error;
    qint64 interval() const {return notifications?30000:5000;}
    bool begin(qint64 now) {
        if(pending||subscribing||now<nextAttempt)return false;
        pending=true;requestedAt=now;requestNotificationRevision=notificationRevision;++token;return true;
    }
    bool confirm(qint64 now,bool notification=false) {
        const bool resume=pending||uncertain;
        if(!notification)pending=false;else {notifiedAt=now;++notificationRevision;}
        uncertain=false;failures=0;error.clear();
        confirmedAt=now;nextAttempt=now+interval();return resume;
    }
    bool readSuperseded() const {return requestedAt>=0&&notificationRevision!=requestNotificationRevision;}
    bool fail(quint64 request,qint64 now,bool known,const QString &reason) {
        if(!pending||request!=token)return false;
        pending=false;++failures;error=reason;
        uncertain=!known||!notifications||failures>=3;
        nextAttempt=now+std::min(failures*1000,5000);return true;
    }
    void reset() {const auto generation=token+1;*this=SwitchQuery{};token=generation;}
};
