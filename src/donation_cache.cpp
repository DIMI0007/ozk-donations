#include "donation_cache.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <windows.h>
#include <wincrypt.h>

namespace {
QString digest(const QString& token) {
    if(token.isEmpty())return {};
    return QString::fromLatin1(QCryptographicHash::hash(token.toUtf8(),QCryptographicHash::Sha256).toHex());
}
QByteArray protect(const QByteArray& plain) {
    DATA_BLOB in{DWORD(plain.size()),reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB out{};
    if(!CryptProtectData(&in,L"OZK Donations cache",nullptr,nullptr,nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN,&out))return {};
    QByteArray encrypted(reinterpret_cast<const char*>(out.pbData),int(out.cbData));
    SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return encrypted;
}
QByteArray unprotect(const QByteArray& encrypted) {
    DATA_BLOB in{DWORD(encrypted.size()),reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()))};
    DATA_BLOB out{};
    if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return {};
    QByteArray plain(reinterpret_cast<const char*>(out.pbData),int(out.cbData));
    SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return plain;
}
}
QString donationTokenDigest(const QString& token){return digest(token);}
QJsonObject loadProtectedDonationJson(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024)return {};
    return QJsonDocument::fromJson(unprotect(file.readAll())).object();
}
bool saveProtectedDonationJson(const QString& path,const QJsonObject& value) {
    if(path.isEmpty()||!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    const auto encrypted=protect(QJsonDocument(value).toJson(QJsonDocument::Compact));
    if(encrypted.isEmpty())return false;
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    return file.write(encrypted)==encrypted.size()&&file.commit();
}
QVector<DonationEntry> loadDonationCache(const QString& path,const QString& daToken,const QString& dpToken) {
    QVector<DonationEntry> entries;
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>512*1024)return entries;
    const auto raw=file.readAll();if(raw.isEmpty())return entries;
    const auto json=QJsonDocument::fromJson(unprotect(raw)).object();
    if(json.value("version").toInt()!=1)return entries;
    const auto daKey=digest(daToken),dpKey=digest(dpToken);
    const auto rows=json.value("entries").toArray();
    for(const auto& value:rows) {
        if(entries.size()>=100)break;
        const auto o=value.toObject();DonationEntry e;
        e.service=o.value("service").toString();e.id=o.value("id").toString();
        if(e.service=="DonationAlerts"){
            if(daKey.isEmpty()||o.value("account").toString()!=daKey)continue;
        }else if(e.service=="DonatePay"){
            if(dpKey.isEmpty()||o.value("account").toString()!=dpKey)continue;
        }else continue;
        e.donor=o.value("donor").toString().left(80);
        e.message=o.value("message").toString().left(600);
        e.currency=o.value("currency").toString().left(8);
        e.amount=o.value("amount").toDouble();
        e.at=QDateTime::fromString(o.value("at").toString(),Qt::ISODateWithMs);
        e.session=false;
        if(e.valid()&&e.at.isValid())entries.append(e);
    }
    return entries;
}
bool saveDonationCache(const QString& path,const QString& daToken,const QString& dpToken,
                       const QVector<DonationEntry>& entries) {
    if(path.isEmpty()||!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QJsonArray rows;const auto daKey=digest(daToken),dpKey=digest(dpToken);
    for(const auto& e:entries) {
        if(rows.size()>=100)break;
        if(!e.valid()||!e.at.isValid())continue;
        const auto account=e.service=="DonationAlerts"?daKey:e.service=="DonatePay"?dpKey:QString();
        if(account.isEmpty())continue;
        rows.append(QJsonObject{{"service",e.service},{"account",account},{"id",e.id},
            {"donor",e.donor.left(80)},{"message",e.message.left(600)},
            {"currency",e.currency},{"amount",e.amount},{"at",e.at.toString(Qt::ISODateWithMs)}});
    }
    const auto encrypted=protect(QJsonDocument(QJsonObject{{"version",1},{"entries",rows}}).toJson(QJsonDocument::Compact));
    if(encrypted.isEmpty())return false;
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    if(file.write(encrypted)!=encrypted.size())return false;
    return file.commit();
}
