#pragma once
#include <QString>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <cmath>
struct ManualService {QString id,name;qint64 kopecks=0;};
inline QVector<ManualService> parseManualServices(const QJsonArray& rows){
    QVector<ManualService> result;QSet<QString> ids;
    for(const auto& row:rows){if(result.size()>=100)break;const auto o=row.toObject();
        const auto id=o.value("id").toString(),name=o.value("name").toString().trimmed().left(80);
        const double amount=o.value("kopecks").toDouble(-1);
        if(id.isEmpty()||id.size()>80||name.isEmpty()||ids.contains(id)||!std::isfinite(amount)||amount<0||amount>1e14||std::floor(amount)!=amount)continue;
        ids.insert(id);result.append({id,name,qint64(amount)});
    }return result;
}
inline qint64 manualServiceTotal(const QVector<ManualService>& rows){qint64 total=0;for(const auto& row:rows)total+=row.kopecks;return total;}
inline QVector<ManualService> loadManualServices(const QString& path){QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()>65536)return {};
    return parseManualServices(QJsonDocument::fromJson(file.readAll()).object().value("services").toArray());
}
inline bool saveManualServices(const QString& path,const QVector<ManualService>& rows){
    if(rows.size()>100||path.isEmpty())return false;QJsonArray array;
    for(const auto& row:rows)array.append(QJsonObject{{"id",row.id},{"name",row.name},{"kopecks",double(row.kopecks)}});
    if(parseManualServices(array).size()!=rows.size())return false;
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly))return false;
    const auto data=QJsonDocument(QJsonObject{{"version",1},{"services",array}}).toJson();
    return file.write(data)==data.size()&&file.commit();
}
