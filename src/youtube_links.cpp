#include "youtube_links.hpp"
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {
QString videoId(const QString& raw) {
    const QUrl url(raw,QUrl::StrictMode);
    if(!url.isValid()||url.userInfo().size()||
       (url.scheme()!="https"&&url.scheme()!="http")||
       (url.port()!=-1&&url.port()!=80&&url.port()!=443))return {};
    const QString host=url.host().toLower();
    QString id;
    const QStringList path=url.path().split('/',Qt::SkipEmptyParts);
    if(host=="youtu.be"||host=="www.youtu.be") {
        if(!path.isEmpty())id=path.first();
    } else if(host=="youtube.com"||host=="www.youtube.com"||
              host=="m.youtube.com"||host=="music.youtube.com"||
              host=="youtube-nocookie.com"||host=="www.youtube-nocookie.com") {
        if(path.size()==1&&path.first()=="watch")
            id=QUrlQuery(url).queryItemValue("v",QUrl::FullyDecoded);
        else if(path.size()==2&&
                (path.first()=="shorts"||path.first()=="live"||path.first()=="embed"))
            id=path.at(1);
    }
    static const QRegularExpression valid("^[A-Za-z0-9_-]{11}$");
    return valid.match(id).hasMatch()?id:QString();
}
}
QVector<YouTubeLink> findYouTubeLinks(const QString& message) {
    QVector<YouTubeLink> found;
    static const QRegularExpression urls(R"(https?://[^\s<>"']+)",QRegularExpression::CaseInsensitiveOption);
    auto it=urls.globalMatch(message);
    while(it.hasNext()) {
        const auto match=it.next();QString candidate=match.captured();
        while(!candidate.isEmpty()&&QStringLiteral(".,;:!?)]}").contains(candidate.back()))candidate.chop(1);
        const QString id=videoId(candidate);
        if(!id.isEmpty())found.append({int(match.capturedStart()),int(candidate.size()),id});
    }
    return found;
}
QString donationTextWithYouTubeLinks(const QString& message) {
    const auto links=findYouTubeLinks(message);
    if(links.isEmpty())return {};
    QString html;int offset=0;
    auto appendText=[&html](const QString& part){
        QString escaped=part.toHtmlEscaped();escaped.replace('\n',"<br>");html+=escaped;
    };
    for(const auto& link:links) {
        appendText(message.mid(offset,link.start-offset));
        html+="<a style=\"color:#58caff;text-decoration:underline;\" href=\"ozkvideo:"+
              link.id+"\">"+message.mid(link.start,link.length).toHtmlEscaped()+"</a>";
        offset=link.start+link.length;
    }
    appendText(message.mid(offset));
    return html;
}
