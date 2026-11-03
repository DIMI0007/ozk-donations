#pragma once
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QVariant>

// Process-local, value-only protocol. No DLL pointers, tokens, disk I/O or networking.
// Access is confined to the OBS GUI thread. A late consumer reads the latest snapshot.
namespace ozk_bridge {
inline constexpr char dpProperty[]="ozk.donatepay.snapshot.v1";
inline QByteArray account(const QString& token) {
    return QCryptographicHash::hash(token.toUtf8(),QCryptographicHash::Sha256).toHex();
}
inline void publishDP(const QString& token,const QJsonArray& rows) {
    auto* app=QCoreApplication::instance();
    if(!app||token.isEmpty()||QThread::currentThread()!=app->thread())return;
    QJsonArray safe;
    for(const auto& value:rows){
        const auto row=value.toObject();
        // Keep just display fields and validation flags; never copy credentials or URLs.
        QJsonObject entry;
        for(const auto* key:{"id","transaction_id","what","name","username","donor_name",
                            "comment","message","currency","sum","amount","created_at","date","time",
                            "type","status","is_test","test"}){
            const auto v=row.value(QLatin1String(key));
            if(v.isString())entry.insert(QLatin1String(key),v.toString().left(
                QByteArray(key)=="comment"||QByteArray(key)=="message"?8192:512));
            else if(v.isDouble()||v.isBool())entry.insert(QLatin1String(key),v);
        }
        safe.append(entry);if(safe.size()>=100)break;
    }
    const auto bytes=QJsonDocument(QJsonObject{{"account",QString::fromLatin1(account(token))},
        {"rows",safe}}).toJson(QJsonDocument::Compact);
    if(bytes.size()>1024*1024)return;
    if(app->property(dpProperty).toByteArray()!=bytes)app->setProperty(dpProperty,bytes);
}
inline QJsonArray readDP(const QString& token) {
    auto* app=QCoreApplication::instance();
    if(!app||token.isEmpty()||QThread::currentThread()!=app->thread())return {};
    const auto bytes=app->property(dpProperty).toByteArray();
    if(bytes.size()>1024*1024)return {};
    const auto root=QJsonDocument::fromJson(bytes).object();
    return root.value("account").toString().toLatin1()==account(token)?root.value("rows").toArray():QJsonArray{};
}
}
