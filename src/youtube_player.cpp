#include "youtube_player.hpp"
#include "obs_browser_panel.hpp"
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace {
QByteArray response(int status,const QByteArray& body,const QByteArray& type="text/plain; charset=utf-8") {
    QByteArray out=status==200?"HTTP/1.1 200 OK\r\n":"HTTP/1.1 204 No Content\r\n";
    out+="Content-Type: "+type+"\r\nContent-Length: "+QByteArray::number(body.size())+
         "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
         "Referrer-Policy: strict-origin-when-cross-origin\r\n"
         "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline' https://www.youtube.com; "
         "frame-src https://www.youtube.com https://www.youtube-nocookie.com; connect-src 'self'; "
         "style-src 'unsafe-inline';\r\nConnection: close\r\n\r\n";
    out+=body;return out;
}
QString nonce() {
    auto* random=QRandomGenerator::system();
    return QString::number(random->generate64(),16).rightJustified(16,'0')+
           QString::number(random->generate64(),16).rightJustified(16,'0');
}
}
YouTubePlayer::YouTubePlayer(QWidget* parent):QWidget(parent) {
    setObjectName("youtubePage");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(7,8,7,7);layout->setSpacing(7);
    auto* row=new QHBoxLayout;
    auto* back=new QPushButton(QString::fromUtf8("← К донатам"));row->addWidget(back);
    row->addStretch();external_=new QPushButton(QString::fromUtf8("Открыть на YouTube ↗"));
    external_->hide();row->addWidget(external_);layout->addLayout(row);
    videoFrame_=new QFrame;videoFrame_->setObjectName("videoFrame");
    videoLayout_=new QVBoxLayout(videoFrame_);videoLayout_->setContentsMargins(0,0,0,0);
    videoLayout_->setSpacing(0);layout->addWidget(videoFrame_);
    status_=new QLabel(QString::fromUtf8("Загрузка видео…"));status_->setObjectName("muted");
    status_->setWordWrap(true);layout->addWidget(status_);layout->addStretch();
    connect(back,&QPushButton::clicked,this,[this]{stop();Q_EMIT returnRequested();});
    connect(external_,&QPushButton::clicked,this,[this]{
        if(!videoId_.isEmpty())QDesktopServices::openUrl(QUrl("https://www.youtube.com/watch?v="+videoId_));
        stop();Q_EMIT returnRequested();
    });
    connect(&server_,&QTcpServer::newConnection,this,[this]{
        while(server_.hasPendingConnections()){
            auto* socket=server_.nextPendingConnection();
            if(sockets_.size()>=8){socket->abort();socket->deleteLater();continue;}
            sockets_.insert(socket);socket->setReadBufferSize(8192);
            auto data=std::make_shared<QByteArray>();
            connect(socket,&QTcpSocket::disconnected,this,[this,socket]{sockets_.remove(socket);socket->deleteLater();});
            connect(socket,&QTcpSocket::readyRead,this,[this,socket,data]{
                data->append(socket->readAll());
                if(data->size()>8192){socket->abort();return;}
                if(data->contains("\r\n\r\n"))serve(socket,*data);
            });
            QTimer::singleShot(5000,socket,[socket]{if(socket->state()!=QAbstractSocket::UnconnectedState)socket->disconnectFromHost();});
        }
    });
}
void YouTubePlayer::setStatus(const QString& text){status_->setText(text);}
void YouTubePlayer::resizeEvent(QResizeEvent* event){
    QWidget::resizeEvent(event);
    videoFrame_->setFixedHeight(std::clamp((width()-14)*9/16,150,430));
}
bool YouTubePlayer::play(const QString& videoId) {
    stop();
    static const QRegularExpression valid("^[A-Za-z0-9_-]{11}$");
    if(!valid.match(videoId).hasMatch())return false;
    QCef* cef=obs_browser_init_panel();
    if(!cef){setStatus(QString::fromUtf8("В OBS не найден встроенный браузер."));return false;}
    if(!server_.listen(QHostAddress::LocalHost,0)){
        setStatus(QString::fromUtf8("Не удалось открыть локальный плеер."));return false;
    }
    videoId_=videoId;nonce_=nonce();
    const QString url=QString("http://127.0.0.1:%1/player/%2?nonce=%3")
        .arg(server_.serverPort()).arg(videoId_,nonce_);
    browser_=cef->create_widget(videoFrame_,url.toStdString(),nullptr);
    if(!browser_){server_.close();videoId_.clear();nonce_.clear();
        setStatus(QString::fromUtf8("Не удалось открыть плеер OBS."));return false;}
    browser_->allowAllPopups(false);
    videoLayout_->addWidget(browser_);
    browser_->show();external_->show();
    setStatus(QString::fromUtf8("Загрузка YouTube…"));return true;
}
void YouTubePlayer::stop() {
    server_.close();videoId_.clear();nonce_.clear();external_->hide();
    const auto clients=sockets_.values();
    for(auto* socket:clients)socket->abort();
    if(browser_){
        auto* old=browser_;browser_=nullptr;videoLayout_->removeWidget(old);
        old->hide();old->closeBrowser();old->deleteLater();
    }
}
QByteArray YouTubePlayer::page() const {
    // Both inserted values were strictly validated before this page is served.
    const QString html=QString::fromUtf8(R"HTML(<!doctype html><html lang="ru"><head>
<meta charset="utf-8"><meta name="referrer" content="strict-origin-when-cross-origin">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>html,body,#player{margin:0;width:100%;height:100%;background:#000;overflow:hidden;border:0}</style>
<script>
const videoId="%1";const nonce="%2";
function notify(kind){fetch('/event?nonce='+nonce+'&kind='+encodeURIComponent(kind),{cache:'no-store'}).catch(()=>{});}
let playing=false;
function onYouTubeIframeAPIReady(){
  const frame=document.getElementById('player');
  frame.src='https://www.youtube-nocookie.com/embed/'+videoId+
    '?enablejsapi=1&playsinline=1&autoplay=1&origin='+encodeURIComponent(location.origin);
  new YT.Player('player',{events:{onReady:e=>{notify('ready');e.target.playVideo();},
      onStateChange:e=>{if(e.data===YT.PlayerState.PLAYING){playing=true;notify('playing');}
        if(e.data===YT.PlayerState.ENDED)notify('ended');},
      onError:e=>notify('error:'+e.data),onAutoplayBlocked:()=>notify('blocked')}});
  setTimeout(()=>{if(!playing)notify('stalled');},12000);
}
</script><script src="https://www.youtube.com/iframe_api"></script>
</head><body><iframe id="player" allow="autoplay; encrypted-media" referrerpolicy="strict-origin-when-cross-origin"></iframe></body></html>)HTML").arg(videoId_,nonce_);
    return html.toUtf8();
}
void YouTubePlayer::serve(QTcpSocket* socket,const QByteArray& requestBytes) {
    const auto line=requestBytes.left(requestBytes.indexOf("\r\n"));
    const auto pieces=line.split(' ');
    if(pieces.size()!=3||pieces.first()!="GET"){
        socket->write(response(204,{}));socket->disconnectFromHost();return;
    }
    const QUrl target=QUrl::fromEncoded(pieces.at(1));
    const QUrlQuery query(target);
    const bool authorized=!nonce_.isEmpty()&&query.queryItemValue("nonce")==nonce_;
    if(authorized&&target.path()=="/player/"+videoId_){
        socket->write(response(200,page(),"text/html; charset=utf-8"));
    }else{
        socket->write(response(204,{}));
        if(authorized&&target.path()=="/event"){
            const QString kind=query.queryItemValue("kind");
            if(kind=="ended")QTimer::singleShot(0,this,[this]{stop();Q_EMIT returnRequested();});
            else if(kind=="ready")setStatus(QString::fromUtf8("Плеер открыт. Ждём начала воспроизведения…"));
            else if(kind=="playing")setStatus(QString::fromUtf8("Видео воспроизводится. Если звук не слышен, проверьте микшер OBS."));
            else if(kind=="stalled")setStatus(QString::fromUtf8("Видео не началось. Если YouTube просит подтвердить вход, откройте ролик кнопкой выше."));
            else if(kind=="blocked")setStatus(QString::fromUtf8("Нажмите ▶ в плеере для воспроизведения."));
            else if(kind.startsWith("error:")){
                setStatus(QString::fromUtf8("YouTube не разрешил воспроизвести это видео внутри OBS."));
                external_->show();
            }
        }
    }
    socket->disconnectFromHost();
}
