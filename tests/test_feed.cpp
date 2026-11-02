#include "feed.hpp"
#include "credentials.hpp"
#include "donation_cache.hpp"
#include "currency_conversion.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <cassert>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
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
    return 0;
}
