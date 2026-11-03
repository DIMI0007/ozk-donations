#pragma once
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QDateTime>
#include <QVariant>
#include <QDebug>
#include <memory>
namespace ozkperf {
class Scope {QElapsedTimer clock_;const char* name_;public:explicit Scope(const char* name):name_(name){clock_.start();}~Scope(){
    const auto ms=clock_.elapsed();if(ms<100)return;thread_local qint64 last=0;const auto now=QDateTime::currentMSecsSinceEpoch();
    if(now-last>=10000){last=now;qWarning()<<"[OZK slow operation]"<<name_<<ms<<"ms";}
}};
inline void install(){auto* app=QCoreApplication::instance();if(!app||app->property("ozkUiMonitor").toBool())return;
    app->setProperty("ozkUiMonitor",true);auto clock=std::make_shared<QElapsedTimer>();clock->start();
    auto* timer=new QTimer(app);timer->setInterval(250);
    QObject::connect(timer,&QTimer::timeout,app,[clock]{const auto elapsed=clock->restart();static qint64 last=0;
        const auto now=QDateTime::currentMSecsSinceEpoch();if(elapsed>1000&&now-last>=10000){last=now;qWarning()<<"[OZK UI delay]"<<elapsed<<"ms between GUI ticks; cause not yet attributed";}});timer->start();
}
}
