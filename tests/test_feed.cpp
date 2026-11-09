#include "feed.hpp"
#include "credentials.hpp"
#include "donation_cache.hpp"
#include "currency_conversion.hpp"
#include "manual_services.hpp"
#include "donatepay_reply.hpp"
#include "donation_bridge.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <cassert>

struct DonationFeedTest {
    static void independentStartup() {
        DonationFeed feed;DonationCredentials c;c.dp="test-startup";
        feed.configure(c);QElapsedTimer wait;wait.start();
        while(!feed.bridgeReady_&&wait.elapsed()<2000){QCoreApplication::processEvents();QThread::msleep(1);}
        assert(feed.bridgeReady_&&feed.dpTimer_.isActive());
        feed.daToken_="test-da-reconnect";feed.retryDA();
        assert(feed.dpTimer_.isActive()); // Previously the delayed DP callback used daEpoch_.
        feed.stop(); // No network call; stop both startup/reconnect timers before expiry.
    }
    static void bridge() {
        DonationFeed feed;
        feed.dpToken_="test-account";feed.bridgeReady_=true;feed.dpTotals_.complete=true;
        const auto row=QJsonObject{{"id","synthetic-5000"},{"what","Test donor"},
            {"comment","Test message"},{"sum",5000},{"currency","RUB"},
            {"type","donation"},{"status","success"},
            {"created_at",QDateTime::currentDateTimeUtc().addSecs(1).toString(Qt::ISODate)},
            {"access_token","must-not-cross-bridge"}};
        ozk_bridge::publishDP("test-account",{row});QCoreApplication::processEvents();
        assert(feed.entries().size()==1&&feed.entries()[0].donor=="Test donor");
        assert(feed.entries()[0].message=="Test message"&&feed.entries()[0].session);
        assert(feed.totals("DonatePay").all.value("RUB")==5000);
        assert(!QCoreApplication::instance()->property(ozk_bridge::dpProperty).toByteArray().contains("must-not-cross-bridge"));
        assert(ozk_bridge::readDP("another-account").isEmpty());
        // API polling/history and Notify deliver the same ID: one card and one total.
        auto duplicate=parseDP(row);feed.add(duplicate);feed.collectTotal(duplicate);
        feed.readNotifyDonations();
        assert(feed.entries().size()==1&&feed.totals("DonatePay").all.value("RUB")==5000);
        auto pending=row;pending["id"]="pending";pending["status"]="pending";
        ozk_bridge::publishDP("test-account",{row,pending});QCoreApplication::processEvents();
        assert(feed.entries().size()==1&&feed.totals("DonatePay").all.value("RUB")==5000);
        // A consumer starting after Notify still sees the snapshot, without a request.
        DonationFeed late;late.dpToken_="test-account";late.bridgeReady_=true;late.readNotifyDonations();
        assert(late.entries().size()==1&&late.entries()[0].amount==5000);
        feed.stop();auto second=row;second["id"]="after-stop";
        ozk_bridge::publishDP("test-account",{second});QCoreApplication::processEvents();
        assert(feed.entries().size()==1);
    }
    static void providerFailover() {
        DonationFeed feed;feed.dpToken_="test-provider";feed.bridgeReady_=true;
        ozk_bridge::publishDPProvider("test-provider");
        assert(ozk_bridge::hasDPProvider("test-provider"));
        assert(!ozk_bridge::hasDPProvider("other-account"));
        assert(!ozk_bridge::hasDPProvider("test-provider",
            QDateTime::currentMSecsSinceEpoch()+46000));
        feed.pollDP(); // No network request while Notify owns live polling.
        assert(feed.dpTimer_.isActive()&&feed.dpTimer_.interval()==15000);
        ozk_bridge::clearDPProvider("other-account");
        assert(ozk_bridge::hasDPProvider("test-provider"));
        ozk_bridge::clearDPProvider("test-provider");
        QCoreApplication::processEvents();
        assert(!ozk_bridge::hasDPProvider("test-provider"));
        assert(feed.dpTimer_.isActive()&&feed.dpTimer_.interval()==350);
        feed.stop(); // Stop before the standalone fallback timer can send a request.
    }
    static void daProviderFailover() {
        DonationFeed feed;feed.daToken_="test-da-provider";feed.bridgeReady_=true;
        feed.daTotalsStarted_=true;feed.daTotals_.complete=true;
        ozk_bridge::publishDAProvider("test-da-provider",true);
        feed.syncDABridge();
        assert(feed.usingDABridge_&&!feed.daRetry_.isActive());
        const auto row=QJsonObject{{"id","shared-da-1"},{"username","Test viewer"},
            {"amount",250},{"currency","RUB"},{"message","Hello"},
            {"created_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"access_token","must-not-cross-bridge"}};
        ozk_bridge::publishDA("test-da-provider",row);
        QCoreApplication::processEvents();
        assert(feed.entries().size()==1&&feed.entries()[0].donor=="Test viewer");
        assert(feed.totals("DonationAlerts").all.value("RUB")==250);
        assert(!QCoreApplication::instance()->property(ozk_bridge::daProperty).toByteArray().contains("must-not-cross-bridge"));
        assert(ozk_bridge::readDA("another-account").isEmpty());
        DonationFeed late;late.daToken_="test-da-provider";late.bridgeReady_=true;
        late.syncDABridge();
        assert(late.entries().size()==1);
        late.stop();feed.stop();
        ozk_bridge::clearDAProvider("test-da-provider");
    }
};

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    DonationFeedTest::bridge();
    DonationFeedTest::providerFailover();
    DonationFeedTest::daProviderFailover();
    DonationFeedTest::independentStartup();
    const auto converted=convertToRub({{"RUB",100.0},{"USD",2.0}},{{"USD",90.0}});
    assert(converted.complete&&converted.converted&&converted.amount==280.0);
    const auto missingRate=convertToRub({{"RUB",100.0},{"KZT",1000.0}},{{"USD",90.0}});
    assert(!missingRate.complete&&missingRate.amount==100.0);
    const auto da=parseDA(QJsonObject{{"data",QJsonObject{{"id",42},{"username","Anna"},
        {"amount","150.00"},{"currency","RUB"},{"message","Спасибо!"}}}});
    assert(da.valid()&&da.id=="42"&&da.donor=="Anna"&&da.amount==150);
    const auto historical=parseDA(QJsonObject{{"id",43},{"username","Anna"},
        {"amount",150},{"currency","RUB"},{"created_at","2026-10-20 20:56:00"}});
    assert(historical.valid()&&historical.at.date()==QDate(2026,10,20));
    const auto started=QDateTime::fromString("2026-11-02T11:00:00Z",Qt::ISODate);
    assert(!occurredThisSession(historical,started));
    DonationEntry fresh=historical;fresh.at=started.addSecs(5);
    assert(occurredThisSession(fresh,started));
    const auto oldDP=parseDP(QJsonObject{{"id",79},{"what","Ivan"},{"sum",250},
        {"currency","RUB"},{"type","donation"},{"status","success"},
        {"created_at","2026-10-20 20:56:00"}});
    assert(oldDP.valid()&&!occurredThisSession(oldDP,started));
    const auto test=parseDA(QJsonObject{{"id",1},{"amount",100},{"currency","RUB"},{"is_test",true}});
    assert(!test.valid());
    const auto dp=parseDP(QJsonObject{{"id",77},{"what","Ivan"},{"sum",250},
        {"currency","RUB"},{"type","donation"},{"status","success"}});
    assert(dp.valid()&&dp.donor=="Ivan"&&dp.amount==250);
    const auto pending=parseDP(QJsonObject{{"id",78},{"sum",500},{"currency","RUB"},
        {"type","donation"},{"status","pending"}});
    assert(!pending.valid());
    const auto wrongType=parseDP(QJsonObject{{"id",79},{"sum",500},{"currency","RUB"},
        {"type","follower"},{"status","success"}});
    assert(!wrongType.valid());
    QTemporaryDir dir;assert(dir.isValid());
    const QString own=dir.filePath("own/settings.json");
    const QString notify=dir.filePath("notify/settings.json");
    assert(saveOwnCredentials(own,"own-DA","own-DP"));
    auto c=loadCredentials(own,notify);
    assert(c.da=="own-DA"&&c.dp=="own-DP"&&!c.daFromNotify&&!c.dpFromNotify);
    // Corrupt/unavailable Notify files must leave standalone authorization usable.
    QFile broken(notify);QDir().mkpath(QFileInfo(notify).absolutePath());
    assert(broken.open(QIODevice::WriteOnly));broken.write("invalid");broken.close();
    c=loadCredentials(own,notify);
    assert(c.da=="own-DA"&&c.dp=="own-DP");
    // Real Notify format is a nested donations object containing DPAPI blobs.
    assert(saveOwnCredentials(notify,"notify-DA","notify-DP"));
    auto encrypted=QJsonDocument::fromJson([&]{QFile f(notify);assert(f.open(QIODevice::ReadOnly));return f.readAll();}()).object();
    const QJsonObject wrapped{{"donations",QJsonObject{{"da_secret",encrypted.value("da_secret")},
        {"dp_secret",encrypted.value("dp_secret")}}}};
    assert(broken.open(QIODevice::WriteOnly|QIODevice::Truncate));
    broken.write(QJsonDocument(wrapped).toJson());broken.close();
    c=loadCredentials(own,notify);
    assert(c.da=="own-DA"&&c.dp=="notify-DP"&&!c.daFromNotify&&c.dpFromNotify);
    assert(saveServiceChoices(own,false,true));
    c=loadCredentials(own,notify);
    assert(!c.daEnabled&&c.dpEnabled&&!c.daFromNotify&&c.dpFromNotify);
    const QString cache=dir.filePath("own/donations-cache.bin");
    DonationEntry cached=da;cached.at=QDateTime::currentDateTime();cached.session=false;
    assert(saveDonationCache(cache,"own-DA","own-DP",{cached}));
    QFile saved(cache);assert(saved.open(QIODevice::ReadOnly));
    assert(!saved.readAll().contains("Anna"));saved.close();
    const auto restored=loadDonationCache(cache,"own-DA","own-DP");
    assert(restored.size()==1&&restored.first().donor=="Anna"&&!restored.first().session);
    assert(loadDonationCache(cache,"different-account","own-DP").isEmpty());
    const QString totalsCache=dir.filePath("own/donations-cache.bin.totals");
    const QJsonObject totalsData{{"version",1},{"account",donationTokenDigest("own-DA")},
        {"rub",1234.5}};
    assert(saveProtectedDonationJson(totalsCache,totalsData));
    assert(loadProtectedDonationJson(totalsCache)==totalsData);
    QFile encryptedTotals(totalsCache);assert(encryptedTotals.open(QIODevice::ReadOnly));
    assert(!encryptedTotals.readAll().contains("1234.5"));
    const auto manualPath=dir.filePath("manual-services.json");
    const QVector<ManualService> manual{{"one","Other A",123456},{"two","Other B",789}};
    assert(manualServiceTotal(manual)==124245);
    assert(saveManualServices(manualPath,manual));
    const auto loaded=loadManualServices(manualPath);assert(loaded.size()==2&&loaded[0].name=="Other A"&&manualServiceTotal(loaded)==124245);
    auto changed=loaded;changed[0].kopecks=10000;changed.removeLast();assert(saveManualServices(manualPath,changed));
    assert(manualServiceTotal(loadManualServices(manualPath))==10000);
    changed[0].kopecks=-1;assert(!saveManualServices(manualPath,changed));
    assert(manualServiceTotal(loadManualServices(manualPath))==10000);
    assert(parseManualServices(QJsonArray{QJsonObject{{"id","bad"},{"name","Bad"},{"kopecks",1.5}}}).isEmpty());
    assert(saveManualServices(manualPath,{}));assert(loadManualServices(manualPath).isEmpty());
    assert(sn::parseDonatePayReply(200,R"({"status":429})").rateLimited);
    const auto temporary=sn::parseDonatePayReply(200,R"({"status":"error","message":"Temporary error"})");
    assert(!temporary.ok&&!temporary.permanent);
    const auto latest=sn::parseDonatePayReply(200,R"({"status":"success","data":[{"id":990001,"sum":5000,"currency":"RUB","type":"donation","status":"success","created_at":"2026-11-03 12:58:14"}]})");
    assert(latest.ok&&latest.rows.size()==1);const auto missed=parseDP(latest.rows[0].toObject());assert(missed.valid()&&missed.amount==5000);
    return 0;
}
