#pragma once
#include <QNetworkReply>
#include <algorithm>
#include <QObject>
#include <atomic>
#include <cstdint>
#include <memory>
#include <windows.h>

namespace donations_telemetry {
inline std::atomic<std::uint64_t> sent{0},received{0},cpuNs{0},dataBytes{0};
inline bool threadTime(std::uint64_t& ns) {
    FILETIME created,ended,kernel,user;
    if(!GetThreadTimes(GetCurrentThread(),&created,&ended,&kernel,&user))return false;
    ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    ns=(k.QuadPart+u.QuadPart)*100;return true;
}
class CpuScope {
public:
    CpuScope():outer_(depth_++==0){valid_=outer_&&threadTime(start_);}
    ~CpuScope(){--depth_;std::uint64_t end=0;
        if(valid_&&threadTime(end)&&end>=start_)cpuNs.fetch_add(end-start_);}
private:
    inline static thread_local unsigned depth_=0;
    bool outer_=false,valid_=false;std::uint64_t start_=0;
};
inline QNetworkReply* watch(QNetworkReply* reply) {
    struct Progress {qint64 up=0,down=0;};
    auto progress=std::make_shared<Progress>();
    QObject::connect(reply,&QNetworkReply::uploadProgress,reply,[progress](qint64 n,qint64){
        if(n>progress->up)sent.fetch_add(std::uint64_t(n-progress->up));
        progress->up=std::max(progress->up,n);
    });
    QObject::connect(reply,&QNetworkReply::downloadProgress,reply,[progress](qint64 n,qint64){
        if(n>progress->down)received.fetch_add(std::uint64_t(n-progress->down));
        progress->down=std::max(progress->down,n);
    });
    return reply;
}
}
