#include "credentials.hpp"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#ifndef OZK_DONATIONS_TEST
#include <obs-module.h>
#include <util/bmem.h>
#endif
#include <windows.h>
#include <wincrypt.h>

namespace {
QString decode(const QString& encoded) {
    const QByteArray bytes = QByteArray::fromBase64(encoded.toLatin1());
    if(bytes.isEmpty()) return {};
    DATA_BLOB in{DWORD(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))};
    DATA_BLOB out{};
    if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) return {};
    const QString value=QString::fromUtf8(reinterpret_cast<const char*>(out.pbData),int(out.cbData));
    SecureZeroMemory(out.pbData,out.cbData);
    LocalFree(out.pbData);
    return value;
}
QString encode(const QString& value) {
    const QByteArray bytes=value.toUtf8();
    if(bytes.isEmpty())return {};
    DATA_BLOB in{DWORD(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))};
    DATA_BLOB out{};
    if(!CryptProtectData(&in,L"OZK Donations",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return {};
    const QByteArray result(reinterpret_cast<const char*>(out.pbData),int(out.cbData));
    SecureZeroMemory(out.pbData,out.cbData);
    LocalFree(out.pbData);
    return QString::fromLatin1(result.toBase64());
}
QJsonObject read(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}

QString ownSettingsPath() {
#ifndef OZK_DONATIONS_TEST
    char* raw=obs_module_config_path("settings.json");
    if(!raw)return {};
    const QString path=QString::fromUtf8(raw);bfree(raw);return path;
#else
    return {};
#endif
}
QString notifySettingsPath() {
    const QString own=ownSettingsPath();
    if(own.isEmpty())return {};
    return QFileInfo(own).dir().filePath("../stream-notify/settings.json");
}
DonationCredentials loadCredentials(const QString& ownSettings,
                                    const QString& notifySettings) {
    DonationCredentials c;
    const auto own=read(ownSettings);
    c.daEnabled=own.value("da_enabled").toBool(true);
    c.dpEnabled=own.value("dp_enabled").toBool(true);
    c.da=decode(own.value("da_secret").toString());
    c.dp=decode(own.value("dp_secret").toString());
    const auto notify=read(notifySettings).value("donations").toObject();
    const auto da=decode(notify.value("da_secret").toString());
    const auto dp=decode(notify.value("dp_secret").toString());
    // Notify's current authorization takes precedence. A standalone key remains
    // available if Notify is removed or the service is signed out there.
    if(!da.isEmpty()){c.da=da;c.daFromNotify=true;}
    if(!dp.isEmpty()){c.dp=dp;c.dpFromNotify=true;}
    return c;
}
bool saveOwnCredentials(const QString& path,const QString& da,const QString& dp) {
    if(path.isEmpty())return false;
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    const QString encodedDA=encode(da),encodedDP=encode(dp);
    if((!da.isEmpty()&&encodedDA.isEmpty())||(!dp.isEmpty()&&encodedDP.isEmpty()))return false;
    QJsonObject data=read(path);
    data["version"]=1;data["da_secret"]=encodedDA;data["dp_secret"]=encodedDP;
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly))return false;
    if(file.write(QJsonDocument(data).toJson())<0)return false;
    return file.commit();
}
bool saveServiceChoices(const QString& path,bool daEnabled,bool dpEnabled) {
    if(path.isEmpty()||!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QJsonObject data=read(path);data["version"]=1;
    data["da_enabled"]=daEnabled;data["dp_enabled"]=dpEnabled;
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly))return false;
    if(file.write(QJsonDocument(data).toJson())<0)return false;
    return file.commit();
}
