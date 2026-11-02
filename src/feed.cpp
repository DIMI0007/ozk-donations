#include "feed.hpp"
#include "donation_cache.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace {
QString str(const QJsonValue& v) {
    if(v.isString())return v.toString();
    if(v.isDouble())return QString::number(v.toDouble(),'f',0);
    return {};
}
QString first(const QJsonObject& o,std::initializer_list<const char*> names) {
    for(auto* key:names){const auto v=str(o.value(QLatin1String(key)));if(!v.isEmpty())return v;}
    return {};
}
QJsonObject unwrap(QJsonObject o) {
    for(int i=0;i<4&&o.value("data").isObject();++i)o=o.value("data").toObject();
    return o;
}
bool flag(const QJsonValue& v) {
    return v.toBool()||(v.isDouble()&&v.toInt()==1)||
           (v.isString()&&(v.toString()=="1"||v.toString().compare("true",Qt::CaseInsensitive)==0));
}
double amount(const QJsonValue& v) {
    bool ok=false;
    const double n=v.isString()?v.toString().toDouble(&ok):v.toDouble();
    if(!v.isString())ok=v.isDouble();
    return ok&&std::isfinite(n)&&n>0&&n<1e12?n:0;
}
QDateTime time(const QJsonObject& o) {
    const QString value=first(o,{"created_at","date","time"});
    auto date=QDateTime::fromString(value,Qt::ISODate);
    if(!date.isValid())date=QDateTime::fromString(value,"yyyy-MM-dd HH:mm:ss");
    if(!date.isValid()&&o.value("created_at").isDouble())
        date=QDateTime::fromSecsSinceEpoch(qint64(o.value("created_at").toDouble()));
    return date.isValid()?date.toLocalTime():QDateTime::currentDateTime();
}
QNetworkRequest request(const QUrl& url) {
    QNetworkRequest r(url);r.setTransferTimeout(10000);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    return r;
}
}
DonationEntry parseDA(const QJsonObject& packet) {
    const auto o=unwrap(packet);DonationEntry e;e.service="DonationAlerts";
    e.id=first(o,{"id"});e.donor=first(o,{"username","name"});
    e.message=first(o,{"message"});e.currency=first(o,{"currency"}).toUpper();
    e.amount=amount(o.value("amount"));e.at=time(o);
    if(flag(o.value("is_test"))||flag(o.value("test"))||flag(o.value("is_fake"))||
       o.value("type").toString()=="test")e.amount=0;
    return e;
}
DonationEntry parseDP(const QJsonObject& packet) {
    const auto o=unwrap(packet);DonationEntry e;e.service="DonatePay";
    e.id=first(o,{"id","transaction_id"});
    e.donor=first(o,{"what","name","username","donor_name"});
    e.message=first(o,{"comment","message"});
    e.currency=first(o,{"currency"}).toUpper();
    e.amount=amount(o.contains("sum")?o.value("sum"):o.value("amount"));e.at=time(o);
    if(flag(o.value("is_test"))||flag(o.value("test"))||
       first(o,{"type"}).compare("donation",Qt::CaseInsensitive)!=0||
       first(o,{"status"}).compare("success",Qt::CaseInsensitive)!=0)e.amount=0;
    return e;
}
bool occurredThisSession(const DonationEntry& entry,const QDateTime& startedAt) {
    return entry.at.isValid()&&startedAt.isValid()&&entry.at.toUTC()>=startedAt.toUTC();
}
DonationFeed::DonationFeed(QObject* parent):QObject(parent) {
    daRetry_.setSingleShot(true);dpTimer_.setSingleShot(true);cacheTimer_.setSingleShot(true);
    cacheTimer_.setInterval(800);
    connect(&cacheTimer_,&QTimer::timeout,this,&DonationFeed::saveCache);
    connect(&daRetry_,&QTimer::timeout,this,&DonationFeed::connectDA);
    connect(&dpTimer_,&QTimer::timeout,this,&DonationFeed::pollDP);
    connect(&socket_,&QWebSocket::connected,this,[this]{
        if(closing_)return;
        QJsonObject hello{{"id",1},{"params",QJsonObject{{"token",daSocketToken_}}}};
        socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(hello).toJson(QJsonDocument::Compact)));
    });
    connect(&socket_,&QWebSocket::textMessageReceived,this,&DonationFeed::receiveDA);
    connect(&socket_,&QWebSocket::disconnected,this,[this]{if(!closing_&&!resettingSocket_)retryDA();});
    connect(&socket_,&QWebSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){
        if(!closing_&&!resettingSocket_)retryDA();
    });
}
void DonationFeed::setStatus(QString& target,const QString& value) {
    if(target!=value){target=value;Q_EMIT updated();}
}
void DonationFeed::configure(const DonationCredentials& c) {
    if(daToken_==c.da&&dpToken_==c.dp&&!closing_)return;
    if(cacheTimer_.isActive()){cacheTimer_.stop();saveCache();}
    ++daEpoch_;++dpEpoch_;daRetry_.stop();dpTimer_.stop();
    resettingSocket_=true;socket_.abort();resettingSocket_=false;
    daToken_=c.da;dpToken_=c.dp;closing_=false;daFailures_=dpFailures_=0;dpPrimed_=false;
    daHistoryPermissionNeeded_=false;daHistoryChecked_=false;
    entries_.clear();seen_.clear();
    for(const auto& entry:loadDonationCache(cachePath_,daToken_,dpToken_)){
        entries_.append(entry);seen_.insert(entry.service+":"+entry.id);
    }
    setStatus(daStatus_,daToken_.isEmpty()?QString::fromUtf8("Не подключён"):QString::fromUtf8("Подключение…"));
    setStatus(dpStatus_,dpToken_.isEmpty()?QString::fromUtf8("Не подключён"):QString::fromUtf8("Подключение…"));
    if(!daToken_.isEmpty())connectDA();
    if(!dpToken_.isEmpty())pollDP();
    Q_EMIT updated();
}
void DonationFeed::stop() {
    if(cacheTimer_.isActive()){cacheTimer_.stop();saveCache();}
    closing_=true;++daEpoch_;++dpEpoch_;daRetry_.stop();dpTimer_.stop();
    resettingSocket_=true;socket_.abort();resettingSocket_=false;
}
void DonationFeed::add(const DonationEntry& entry) {
    if(!entry.valid())return;
    const QString key=entry.service+":"+entry.id;
    if(seen_.contains(key)){
        if(entry.session)for(auto& existing:entries_)
            if(existing.service==entry.service&&existing.id==entry.id&&!existing.session){
                existing.session=true;cacheTimer_.start();Q_EMIT updated();break;
            }
        return;
    }
    seen_.insert(key);entries_.append(entry);
    std::sort(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.at>b.at;});
    if(entries_.size()>100){seen_.remove(entries_.last().service+":"+entries_.last().id);entries_.removeLast();}
    cacheTimer_.start();
    Q_EMIT updated();
}
void DonationFeed::saveCache() {
    if(!cachePath_.isEmpty())saveDonationCache(cachePath_,daToken_,dpToken_,entries_);
}
void DonationFeed::connectDA() {
    if(closing_||daToken_.isEmpty())return;
    const quint64 epoch=++daEpoch_;
    auto req=request(QUrl("https://www.donationalerts.com/api/v1/user/oauth"));
    req.setRawHeader("Authorization",("Bearer "+daToken_).toUtf8());
    auto* reply=http_.get(req);
    connect(reply,&QNetworkReply::finished,this,[this,reply,epoch]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body=QJsonDocument::fromJson(reply->readAll()).object();reply->deleteLater();
        if(closing_||epoch!=daEpoch_)return;
        const auto user=body.value("data").toObject();
        const QString id=first(user,{"id"});daSocketToken_=user.value("socket_connection_token").toString();
        if(code==401||code==403){setStatus(daStatus_,QString::fromUtf8("Нужно войти заново"));return;}
        if(code!=200||id.isEmpty()||daSocketToken_.isEmpty()){retryDA();return;}
        daChannel_="$alerts:donation_"+id;
        if(!daHistoryChecked_)fetchDAHistory();
        socket_.open(QUrl("wss://centrifugo.donationalerts.com/connection/websocket"));
    });
}
void DonationFeed::fetchDAHistory() {
    if(closing_||daToken_.isEmpty()||daHistoryChecked_)return;
    daHistoryChecked_=true;
    const QString token=daToken_;
    auto req=request(QUrl("https://www.donationalerts.com/api/v1/alerts/donations?page=1"));
    req.setRawHeader("Authorization",("Bearer "+daToken_).toUtf8());
    auto* reply=http_.get(req);
    connect(reply,&QNetworkReply::finished,this,[this,reply,token]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body=QJsonDocument::fromJson(reply->readAll()).object();reply->deleteLater();
        if(closing_||token!=daToken_)return;
        if(code==401||code==403){
            daHistoryPermissionNeeded_=true;Q_EMIT updated();return;
        }
        if(code!=200||!body.value("data").isArray()){
            daHistoryChecked_=false;
            QTimer::singleShot(60000,this,[this]{if(!closing_)fetchDAHistory();});
            return;
        }
        for(const auto& value:body.value("data").toArray())if(value.isObject()){
            auto entry=parseDA(value.toObject());entry.session=false;add(entry);
        }
        Q_EMIT updated();
    });
}
void DonationFeed::subscribeDA(const QString& client) {
    auto req=request(QUrl("https://www.donationalerts.com/api/v1/centrifuge/subscribe"));
    req.setRawHeader("Authorization",("Bearer "+daToken_).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    const quint64 epoch=daEpoch_;
    const auto data=QJsonDocument(QJsonObject{{"channels",QJsonArray{daChannel_}},{"client",client}}).toJson();
    auto* reply=http_.post(req,data);
    connect(reply,&QNetworkReply::finished,this,[this,reply,epoch]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body=QJsonDocument::fromJson(reply->readAll()).object();reply->deleteLater();
        if(closing_||epoch!=daEpoch_)return;
        const auto channels=body.value("channels").toArray();
        const QString token=channels.isEmpty()?QString():channels.first().toObject().value("token").toString();
        if(code==401||code==403){setStatus(daStatus_,QString::fromUtf8("Нет доступа"));return;}
        if(code!=200||token.isEmpty()){retryDA();return;}
        const QJsonObject sub{{"id",2},{"method",1},{"params",QJsonObject{{"channel",daChannel_},{"token",token}}}};
        socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(sub).toJson(QJsonDocument::Compact)));
    });
}
void DonationFeed::receiveDA(const QString& text) {
    const auto packet=QJsonDocument::fromJson(text.toUtf8()).object();
    const auto result=packet.value("result").toObject();
    if(packet.value("id").toInt()==1&&result.contains("client")){
        subscribeDA(result.value("client").toString());return;
    }
    if(packet.value("id").toInt()==2&&!packet.contains("error")){
        daFailures_=0;setStatus(daStatus_,QString::fromUtf8("Подключён"));return;
    }
    if(packet.contains("error")){retryDA();return;}
    if(result.value("channel").toString()!=daChannel_||result.value("type").toInt()==1)return;
    const auto payload=result.value("data").toObject();
    const QString reason=payload.value("reason").toString();
    if(!reason.isEmpty()&&reason!="create"&&reason!="new")return;
    auto entry=parseDA(payload);
    entry.session=occurredThisSession(entry,startedAt_);
    add(entry);
}
void DonationFeed::retryDA() {
    if(closing_||daToken_.isEmpty())return;
    ++daEpoch_;resettingSocket_=true;socket_.abort();resettingSocket_=false;
    daRetry_.stop();daFailures_=std::min(daFailures_+1,6);
    daRetry_.start(std::min(120000,2000*(1<<daFailures_))+QRandomGenerator::global()->bounded(1000));
    setStatus(daStatus_,QString::fromUtf8("Переподключение…"));
}
void DonationFeed::pollDP() {
    if(closing_||dpToken_.isEmpty())return;
    const quint64 epoch=++dpEpoch_;
    QUrl url("https://donatepay.ru/api/v1/transactions");QUrlQuery query;
    query.addQueryItem("access_token",dpToken_);query.addQueryItem("limit","100");
    query.addQueryItem("type","donation");query.addQueryItem("status","success");
    query.addQueryItem("order","DESC");url.setQuery(query);
    auto* reply=http_.get(request(url));
    connect(reply,&QNetworkReply::finished,this,[this,reply,epoch]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes=reply->readAll();const auto retryAfter=reply->rawHeader("Retry-After");
        reply->deleteLater();if(closing_||epoch!=dpEpoch_)return;
        const auto body=QJsonDocument::fromJson(bytes).object();
        const auto status=body.value("status").toString();
        if(code==401||code==403||status=="error"){
            setStatus(dpStatus_,QString::fromUtf8("Проверьте ключ"));return;
        }
        if(code!=200||body.isEmpty()){
            setStatus(dpStatus_,QString::fromUtf8("Переподключение…"));
            dpFailures_=std::min(dpFailures_+1,3);
            int delay=30000*(1<<dpFailures_);
            if(code==429){bool ok=false;const int seconds=retryAfter.toInt(&ok);
                if(ok&&seconds>0)delay=std::max(delay,std::min(seconds,3600)*1000);}
            dpTimer_.start(std::min(delay,3600000));return;
        }
        QJsonArray rows=body.value("data").toArray();
        if(rows.isEmpty()&&body.value("data").isObject())rows=body.value("data").toObject().value("data").toArray();
        if(rows.isEmpty())rows=body.value("transactions").toArray();
        if(!body.value("data").isArray()&&!body.value("data").isObject()&&!body.value("transactions").isArray()){
            setStatus(dpStatus_,QString::fromUtf8("Неизвестный ответ API"));return;
        }
        for(const auto& row:rows)if(row.isObject()){
            auto donation=parseDP(row.toObject());donation.session=dpPrimed_;
            add(donation);
        }
        dpPrimed_=true;
        dpFailures_=0;setStatus(dpStatus_,QString::fromUtf8("Подключён"));
        dpTimer_.start(60000); // Notify also polls; keep combined request rate modest.
    });
}
