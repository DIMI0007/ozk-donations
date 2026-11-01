#include "feed.hpp"
#include "credentials.hpp"
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
    const auto da=parseDA(QJsonObject{{"data",QJsonObject{{"id",42},{"username","Anna"},
        {"amount","150.00"},{"currency","RUB"},{"message","Спасибо!"}}}});
    assert(da.valid()&&da.id=="42"&&da.donor=="Anna"&&da.amount==150);
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
    assert(c.da=="notify-DA"&&c.dp=="notify-DP"&&c.daFromNotify&&c.dpFromNotify);
    assert(saveServiceChoices(own,false,true));
    c=loadCredentials(own,notify);
    assert(!c.daEnabled&&c.dpEnabled&&c.daFromNotify&&c.dpFromNotify);
    return 0;
}
