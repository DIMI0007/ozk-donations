#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace sn {
struct DonatePayReply {bool ok=false;bool permanent=false;bool rateLimited=false;QString status;QJsonArray rows;};
inline DonatePayReply parseDonatePayReply(int http,const QByteArray& bytes) {
    DonatePayReply out;
    if(http==401||http==403){out.permanent=true;out.status=QString("DonatePay authorization rejected (%1)").arg(http);return out;}
    if(http==429){out.rateLimited=true;out.status="Rate limited";return out;}
    if(http!=200){out.status=http?QString("HTTP %1").arg(http):"Network error";return out;}
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject()){
        out.permanent=true;out.status="Unrecognized API response";return out;
    }
    const auto object=document.object();const auto status=object.value("status");
    if(status.toInt()==429){out.rateLimited=true;out.status="Rate limited";return out;}
    if(status.toString().compare("error",Qt::CaseInsensitive)==0){
        const auto message=object.value("message").toString().trimmed();out.permanent=true;
        out.status=message.compare("Incorrect token",Qt::CaseInsensitive)==0?"Incorrect DonatePay API key":"DonatePay API error";return out;
    }
    if(object.value("data").isArray())out.rows=object.value("data").toArray();
    else if(object.value("data").isObject()&&object.value("data").toObject().value("data").isArray())out.rows=object.value("data").toObject().value("data").toArray();
    else if(object.value("transactions").isArray())out.rows=object.value("transactions").toArray();
    else {out.permanent=true;out.status="Unrecognized API response";return out;}
    out.ok=true;out.status="API connected (HTTP polling)";return out;
}
}

