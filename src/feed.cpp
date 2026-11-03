#include "feed.hpp"
#include "donatepay_reply.hpp"
#include "donation_cache.hpp"
#include "telemetry.hpp"
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
QJsonObject mapJson(const QMap<QString,double>& values) {
    QJsonObject result;for(auto it=values.cbegin();it!=values.cend();++it)result.insert(it.key(),it.value());return result;
}
QMap<QString,double> jsonMap(const QJsonObject& value) {
    QMap<QString,double> result;
    for(auto it=value.begin();it!=value.end();++it)if(it.value().isDouble()&&std::isfinite(it.value().toDouble()))
        result.insert(it.key(),it.value().toDouble());
    return result;
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
    cacheWorker_.setMaxThreadCount(1);
    cacheTimer_.setInterval(1200);
    totalsCacheTimer_.setSingleShot(true);totalsCacheTimer_.setInterval(2200);
    connect(&totalsCacheTimer_,&QTimer::timeout,this,&DonationFeed::writeTotalsCache);
    connect(&cacheTimer_,&QTimer::timeout,this,&DonationFeed::saveCache);
    connect(&daRetry_,&QTimer::timeout,this,&DonationFeed::connectDA);
    connect(&dpTimer_,&QTimer::timeout,this,&DonationFeed::pollDP);
    connect(&socket_,&QWebSocket::connected,this,[this]{
        if(closing_)return;
        QJsonObject hello{{"id",1},{"params",QJsonObject{{"token",daSocketToken_}}}};
        const auto message=QJsonDocument(hello).toJson(QJsonDocument::Compact);
        donations_telemetry::sent.fetch_add(std::uint64_t(message.size()));
        socket_.sendTextMessage(QString::fromUtf8(message));
    });
    connect(&socket_,&QWebSocket::textMessageReceived,this,&DonationFeed::receiveDA);
    connect(&socket_,&QWebSocket::disconnected,this,[this]{if(!closing_&&!resettingSocket_)retryDA();});
    connect(&socket_,&QWebSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){
        if(!closing_&&!resettingSocket_)retryDA();
    });
}
DonationFeed::~DonationFeed() { stop();cacheWorker_.waitForDone(); }
void DonationFeed::setStatus(QString& target,const QString& value) {
    if(target!=value){target=value;Q_EMIT updated();}
}
void DonationFeed::configure(const DonationCredentials& c) {
    donations_telemetry::CpuScope cpu;
    if(daToken_==c.da&&dpToken_==c.dp&&!closing_)return;
    if(cacheTimer_.isActive()){cacheTimer_.stop();saveCache();}
    if(totalsCacheTimer_.isActive()){totalsCacheTimer_.stop();writeTotalsCache();}
    ++daEpoch_;++dpEpoch_;daRetry_.stop();dpTimer_.stop();
    resettingSocket_=true;socket_.abort();resettingSocket_=false;
    daToken_=c.da;dpToken_=c.dp;closing_=false;daFailures_=dpFailures_=0;
    daHistoryPermissionNeeded_=false;daHistoryChecked_=false;
    daTotals_={};dpTotals_={};daTotalIds_.clear();dpTotalIds_.clear();
    daTotalsStarted_=dpTotalsStarted_=false;
    daNextPage_=1;dpNextSkip_=0;
    entries_.clear();seen_.clear();
    setStatus(daStatus_,daToken_.isEmpty()?QString::fromUtf8("Не подключён"):QString::fromUtf8("Подключение…"));
    setStatus(dpStatus_,dpToken_.isEmpty()?QString::fromUtf8("Не подключён"):QString::fromUtf8("Подключение…"));
    const auto path=cachePath_,da=daToken_,dp=dpToken_;const auto epoch=daEpoch_;
    cacheWorker_.start([this,path,da,dp,epoch]{
        const auto totals=loadProtectedDonationJson(path+".totals");
        const auto entries=loadDonationCache(path,da,dp);
        QMetaObject::invokeMethod(this,[this,epoch,entries,totals]{
            if(closing_||epoch!=daEpoch_)return;
            restoreTotalsCache(totals);
            for(const auto& entry:entries){entries_.append(entry);seen_.insert(entry.service+":"+entry.id);}
            if(!daToken_.isEmpty())connectDA();
            if(!dpToken_.isEmpty())QTimer::singleShot(350,this,[this,epoch]{if(!closing_&&epoch==daEpoch_)pollDP();});
            Q_EMIT updated();
        },Qt::QueuedConnection);
    });
    Q_EMIT updated();
}
void DonationFeed::stop() {
    if(cacheTimer_.isActive()){cacheTimer_.stop();saveCache();}
    totalsCacheTimer_.stop();writeTotalsCache();
    closing_=true;++daEpoch_;++dpEpoch_;daRetry_.stop();dpTimer_.stop();
    resettingSocket_=true;socket_.abort();resettingSocket_=false;
}
void DonationFeed::add(const DonationEntry& entry) {
    donations_telemetry::CpuScope cpu;
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
    updateDataUsage();
    Q_EMIT updated();
}
void DonationFeed::updateDataUsage() {
    std::uint64_t bytes=std::uint64_t(entries_.capacity())*sizeof(DonationEntry);
    for(const auto& e:entries_)for(const auto* text:{&e.service,&e.id,&e.donor,&e.message,&e.currency})
        bytes+=std::uint64_t(text->capacity())*sizeof(QChar);
    for(const auto& set:{&daTotalIds_,&dpTotalIds_}){
        bytes+=std::uint64_t(set->size())*sizeof(QString);
        for(const auto& id:*set)bytes+=std::uint64_t(id.capacity())*sizeof(QChar);
    }
    donations_telemetry::dataBytes.store(bytes);
}
void DonationFeed::saveCache() {
    if(cachePath_.isEmpty())return;
    const auto path=cachePath_, da=daToken_, dp=dpToken_;const auto entries=entries_;
    cacheWorker_.start([path,da,dp,entries]{saveDonationCache(path,da,dp,entries);});
}
void DonationFeed::restoreTotalsCache(const QJsonObject& cache) {
    donations_telemetry::CpuScope cpu;
    if(cachePath_.isEmpty())return;
    if(cache.value("version").toInt()!=1)return;
    const auto month=QDate::currentDate().toString("yyyy-MM");
    auto restore=[&](const QString& key,const QString& token,Totals& totals,QSet<QString>& ids,int& next){
        if(token.isEmpty())return;
        const auto item=cache.value(key).toObject();
        if(item.value("account").toString()!=donationTokenDigest(token))return;
        if(item.value("monthKey").toString()!=month)return; // Recount for a new calendar month.
        totals.all=jsonMap(item.value("all").toObject());
        totals.month=jsonMap(item.value("month").toObject());
        totals.complete=item.value("complete").toBool();
        for(const auto& value:item.value("ids").toArray())if(value.isString())ids.insert(value.toString());
        next=std::max(1,item.value("next").toInt(1));
    };
    restore("da",daToken_,daTotals_,daTotalIds_,daNextPage_);
    restore("dp",dpToken_,dpTotals_,dpTotalIds_,dpNextSkip_);
    updateDataUsage();
    Q_EMIT updated();
}
void DonationFeed::saveTotalsCache() {
    if(!totalsCacheTimer_.isActive())totalsCacheTimer_.start();
}
void DonationFeed::writeTotalsCache() {
    if(cachePath_.isEmpty())return;
    const auto path=cachePath_+".totals";
    const auto daToken=daToken_,dpToken=dpToken_;
    const auto daTotals=daTotals_,dpTotals=dpTotals_;
    const auto daIds=daTotalIds_,dpIds=dpTotalIds_;
    const int daNext=daNextPage_,dpNext=dpNextSkip_;
    cacheWorker_.start([path,daToken,dpToken,daTotals,dpTotals,daIds,dpIds,daNext,dpNext]{
    const auto old=loadProtectedDonationJson(path);
    QJsonObject result{{"version",1}};
    const auto month=QDate::currentDate().toString("yyyy-MM");
    auto insert=[&](const QString& key,const QString& token,const Totals& totals,const QSet<QString>& ids,int next){
        if(token.isEmpty()){
            if(old.value(key).isObject())result.insert(key,old.value(key));return;
        }
        QJsonArray names;for(const auto& id:ids)names.append(id);
        result.insert(key,QJsonObject{{"account",donationTokenDigest(token)},
            {"all",mapJson(totals.all)},{"month",mapJson(totals.month)},
            {"monthKey",month},{"complete",totals.complete},{"next",next},{"ids",names}});
    };
    insert("da",daToken,daTotals,daIds,daNext);
    insert("dp",dpToken,dpTotals,dpIds,dpNext);
    saveProtectedDonationJson(path,result);
    });
}
void DonationFeed::connectDA() {
    donations_telemetry::CpuScope cpu;
    if(closing_||daToken_.isEmpty())return;
    const quint64 epoch=++daEpoch_;
    auto req=request(QUrl("https://www.donationalerts.com/api/v1/user/oauth"));
    req.setRawHeader("Authorization",("Bearer "+daToken_).toUtf8());
    auto* reply=donations_telemetry::watch(http_.get(req));
    connect(reply,&QNetworkReply::finished,this,[this,reply,epoch]{
        donations_telemetry::CpuScope cpu;
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
    daTotalsStarted_=true;
    fetchDAHistoryPage(1);
}
void DonationFeed::collectTotal(const DonationEntry& entry) {
    if(!entry.valid())return;
    auto& ids=entry.service=="DonationAlerts"?daTotalIds_:dpTotalIds_;
    auto& total=entry.service=="DonationAlerts"?daTotals_:dpTotals_;
    const QString key=entry.service+":"+entry.id;
    if(ids.contains(key))return;
    ids.insert(key);total.all[entry.currency]+=entry.amount;
    const auto date=entry.at.toLocalTime().date();const auto today=QDate::currentDate();
    if(date.year()==today.year()&&date.month()==today.month())total.month[entry.currency]+=entry.amount;
}
void DonationFeed::fetchDAHistoryPage(int page) {
    donations_telemetry::CpuScope cpu;
    if(closing_||daToken_.isEmpty()||page<1||page>10000)return;
    const QString token=daToken_;
    auto req=request(QUrl(QString("https://www.donationalerts.com/api/v1/alerts/donations?page=%1").arg(page)));
    req.setRawHeader("Authorization",("Bearer "+daToken_).toUtf8());
    auto* reply=donations_telemetry::watch(http_.get(req));
    connect(reply,&QNetworkReply::finished,this,[this,reply,token,page]{
        donations_telemetry::CpuScope cpu;
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body=QJsonDocument::fromJson(reply->readAll()).object();reply->deleteLater();
        if(closing_||token!=daToken_)return;
        if(code==401||code==403){
            daHistoryPermissionNeeded_=true;daTotalsStarted_=false;Q_EMIT updated();return;
        }
        if(code!=200||!body.value("data").isArray()){
            QTimer::singleShot(60000,this,[this,token,page]{if(!closing_&&token==daToken_)fetchDAHistoryPage(page);});
            return;
        }
        for(const auto& value:body.value("data").toArray())if(value.isObject()){
            auto entry=parseDA(value.toObject());entry.session=false;add(entry);collectTotal(entry);
        }
        daNextPage_=page+1;saveTotalsCache();
        updateDataUsage();
        const auto meta=body.value("meta").toObject();
        const int last=meta.value("last_page").toInt(page);
        if(page<last&&last<=10000&&!(daTotals_.complete&&page==1))
            QTimer::singleShot(3000,this,[this,token,page]{if(!closing_&&token==daToken_)fetchDAHistoryPage(page+1);});
        else if(page>=last){daTotals_.complete=true;saveTotalsCache();Q_EMIT updated();}
        Q_EMIT updated();
    });
}
void DonationFeed::subscribeDA(const QString& client) {
    auto req=request(QUrl("https://www.donationalerts.com/api/v1/centrifuge/subscribe"));
    req.setRawHeader("Authorization",("Bearer "+daToken_).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    const quint64 epoch=daEpoch_;
    const auto data=QJsonDocument(QJsonObject{{"channels",QJsonArray{daChannel_}},{"client",client}}).toJson();
    auto* reply=donations_telemetry::watch(http_.post(req,data));
    connect(reply,&QNetworkReply::finished,this,[this,reply,epoch]{
        donations_telemetry::CpuScope cpu;
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body=QJsonDocument::fromJson(reply->readAll()).object();reply->deleteLater();
        if(closing_||epoch!=daEpoch_)return;
        const auto channels=body.value("channels").toArray();
        const QString token=channels.isEmpty()?QString():channels.first().toObject().value("token").toString();
        if(code==401||code==403){setStatus(daStatus_,QString::fromUtf8("Нет доступа"));return;}
        if(code!=200||token.isEmpty()){retryDA();return;}
        const QJsonObject sub{{"id",2},{"method",1},{"params",QJsonObject{{"channel",daChannel_},{"token",token}}}};
        const auto message=QJsonDocument(sub).toJson(QJsonDocument::Compact);
        donations_telemetry::sent.fetch_add(std::uint64_t(message.size()));
        socket_.sendTextMessage(QString::fromUtf8(message));
    });
}
void DonationFeed::receiveDA(const QString& text) {
    donations_telemetry::CpuScope cpu;
    donations_telemetry::received.fetch_add(std::uint64_t(text.toUtf8().size()));
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
    add(entry);if(daTotalsStarted_){collectTotal(entry);saveTotalsCache();Q_EMIT updated();}
}
void DonationFeed::retryDA() {
    if(closing_||daToken_.isEmpty())return;
    ++daEpoch_;resettingSocket_=true;socket_.abort();resettingSocket_=false;
    daRetry_.stop();daFailures_=std::min(daFailures_+1,6);
    daRetry_.start(std::min(120000,2000*(1<<daFailures_))+QRandomGenerator::global()->bounded(1000));
    setStatus(daStatus_,QString::fromUtf8("Переподключение…"));
}
void DonationFeed::pollDP() {
    donations_telemetry::CpuScope cpu;
    if(closing_||dpToken_.isEmpty())return;
    const quint64 epoch=++dpEpoch_;
    QUrl url("https://donatepay.ru/api/v1/transactions");QUrlQuery query;
    query.addQueryItem("access_token",dpToken_);query.addQueryItem("limit","100");
    query.addQueryItem("type","donation");query.addQueryItem("status","success");
    query.addQueryItem("order","DESC");url.setQuery(query);
    auto* reply=donations_telemetry::watch(http_.get(request(url)));
    connect(reply,&QNetworkReply::finished,this,[this,reply,epoch]{
        donations_telemetry::CpuScope cpu;
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes=reply->readAll();const auto retryAfter=reply->rawHeader("Retry-After");
        reply->deleteLater();if(closing_||epoch!=dpEpoch_)return;
        const auto response=sn::parseDonatePayReply(code,bytes);
        if(!response.ok){
            const bool invalidKey=code==401||code==403||response.status=="Incorrect DonatePay API key";
            setStatus(dpStatus_,invalidKey?QString::fromUtf8("Проверьте ключ"):QString::fromUtf8("Переподключение…"));
            if(invalidKey)return;
            dpFailures_=std::min(dpFailures_+1,3);int delay=30000*(1<<dpFailures_);
            if(response.rateLimited){bool ok=false;const double seconds=retryAfter.toDouble(&ok);
                if(ok&&std::isfinite(seconds)&&seconds>0)delay=std::max(delay,int(std::min(seconds,3600.0)*1000));}
            dpTimer_.start(delay);
            qWarning()<<"[OZK Donations] DonatePay retry scheduled; HTTP"<<code<<"rate limited"<<response.rateLimited<<"delay ms"<<delay;
            return;
        }
        const auto rows=response.rows;
        for(const auto& row:rows)if(row.isObject()){
            auto donation=parseDP(row.toObject());
            donation.session=occurredThisSession(donation,startedAt_);
            add(donation);
            collectTotal(donation); // Stable transaction IDs deduplicate polling and history.
        }
        if(!dpTotalsStarted_){
            dpTotalsStarted_=true;
            if(rows.size()<100)dpTotals_.complete=true;
            else if(!dpTotals_.complete)
                QTimer::singleShot(25000,this,[this,token=dpToken_]{if(!closing_&&token==dpToken_)fetchDPHistory(100);});
        }
        saveTotalsCache();
        Q_EMIT updated();
        dpFailures_=0;setStatus(dpStatus_,QString::fromUtf8("Подключён"));
        dpTimer_.start(60000); // Notify also polls; keep combined request rate modest.
    });
}
void DonationFeed::fetchDPHistory(int skip) {
    donations_telemetry::CpuScope cpu;
    if(closing_||dpToken_.isEmpty()||skip<100||skip>1000000)return;
    const QString token=dpToken_;
    QUrl url("https://donatepay.ru/api/v1/transactions");QUrlQuery query;
    query.addQueryItem("access_token",token);query.addQueryItem("limit","100");
    query.addQueryItem("skip",QString::number(skip));query.addQueryItem("type","donation");
    query.addQueryItem("status","success");query.addQueryItem("order","DESC");url.setQuery(query);
    auto* reply=donations_telemetry::watch(http_.get(request(url)));
    connect(reply,&QNetworkReply::finished,this,[this,reply,token,skip]{
        donations_telemetry::CpuScope cpu;
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes=reply->readAll();reply->deleteLater();
        if(closing_||token!=dpToken_)return;
        const auto response=sn::parseDonatePayReply(code,bytes);
        if(!response.ok){
            QTimer::singleShot(60000,this,[this,token,skip]{if(!closing_&&token==dpToken_)fetchDPHistory(skip);});return;
        }
        const auto rows=response.rows;
        const int before=dpTotalIds_.size();
        for(const auto& row:rows)if(row.isObject())collectTotal(parseDP(row.toObject()));
        dpNextSkip_=skip+100;saveTotalsCache();
        updateDataUsage();
        if(rows.size()<100){dpTotals_.complete=true;saveTotalsCache();Q_EMIT updated();return;}
        if(dpTotalIds_.size()==before){return;} // Unknown pagination; do not show an incomplete total.
        QTimer::singleShot(25000,this,[this,token,skip]{if(!closing_&&token==dpToken_)fetchDPHistory(skip+100);});
        Q_EMIT updated();
    });
}
