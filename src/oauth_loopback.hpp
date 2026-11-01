#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QRandomGenerator>
#include <QPointer>
#include <QSet>

// Temporary IPv4-loopback receiver for documented native/implicit OAuth flows.
// No remote interface, credentials in logs, external scripts, or persistent server.
class OAuthLoopback final : public QObject {
    Q_OBJECT
public:
    explicit OAuthLoopback(QObject* parent=nullptr):QObject(parent) {
        timeout_.setSingleShot(true);
        connect(&timeout_,&QTimer::timeout,this,[this]{stop();Q_EMIT failed("Authorization timed out");});
        connect(&server_,&QTcpServer::newConnection,this,[this]{
            while(server_.hasPendingConnections()) {
                auto* socket=server_.nextPendingConnection();
                if(sockets_.size()>=4){socket->abort();socket->deleteLater();continue;}
                sockets_.insert(socket);socket->setReadBufferSize(32769);
                connect(socket,&QTcpSocket::disconnected,this,[this,socket]{sockets_.remove(socket);socket->deleteLater();});
                connect(socket,&QTcpSocket::readyRead,this,[this,socket]{receive(socket);});
                QTimer::singleShot(5000,socket,[socket]{socket->disconnectFromHost();});
            }
        });
    }
    ~OAuthLoopback() override {stop();}
    bool begin(quint16 port,const QString& callbackPath,bool implicit=true) {
        stop();
        if(!callbackPath.startsWith('/')||!server_.listen(QHostAddress::LocalHost,port))return false;
        path_=callbackPath;implicit_=implicit;
        state_=QString::number(QRandomGenerator::system()->generate64(),16).rightJustified(16,'0')+
            QString::number(QRandomGenerator::system()->generate64(),16).rightJustified(16,'0');
        origin_=QString("http://127.0.0.1:%1").arg(server_.serverPort());
        timeout_.start(180000);return true;
    }
    void stop() {
        timeout_.stop();server_.close();state_.clear();
        const auto sockets=sockets_.values();sockets_.clear();
        for(auto* socket:sockets){socket->disconnect(this);socket->abort();socket->deleteLater();}
    }
    bool active()const{return server_.isListening();}
    QString state()const{return state_;}
    QUrl redirect()const{return QUrl(origin_+path_);}
Q_SIGNALS:
    void received(const QUrlQuery& parameters);
    void failed(const QString& reason);
private:
    void respond(QTcpSocket* socket,int status,const QByteArray& body,const QByteArray& extra={}) {
        QByteArray header="HTTP/1.1 "+QByteArray::number(status)+(status==200?" OK\r\n":" Error\r\n");
        header+="Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nPragma: no-cache\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
        header+=extra;header+="Content-Length: "+QByteArray::number(body.size())+"\r\n\r\n";
        socket->write(header+body);socket->disconnectFromHost();
    }
    void accept(QTcpSocket* socket,const QUrlQuery& parameters) {
        if(state_.isEmpty()||parameters.allQueryItemValues("state").size()!=1||parameters.queryItemValue("state")!=state_) {
            respond(socket,400,"Invalid authorization state. Return to OBS and try again.");return;
        }
        const QString value=parameters.queryItemValue(implicit_?"access_token":"code",QUrl::FullyDecoded);
        const QString error=parameters.queryItemValue("error");
        if(error.isEmpty()&&(value.isEmpty()||value.size()>16384)) {
            respond(socket,400,"Authorization result is missing.");return;
        }
        // Close the listener immediately, leaving this response socket to drain.
        sockets_.remove(socket);timeout_.stop();server_.close();state_.clear();
        respond(socket,200,"<!doctype html><meta charset=utf-8><title>OZK Notify</title><p>Authorization received. Return to OBS.</p>");
        if(!error.isEmpty())Q_EMIT failed("Authorization declined or cancelled");
        else Q_EMIT received(parameters);
    }
    void receive(QTcpSocket* socket) {
        if(!active()||socket->peerAddress()!=QHostAddress(QHostAddress::LocalHost)){socket->abort();return;}
        const QByteArray bytes=socket->peek(32769);
        if(bytes.size()>32768){respond(socket,413,"Request too large");return;}
        const auto end=bytes.indexOf("\r\n\r\n");
        if(end<0){if(bytes.size()>16384)respond(socket,431,"Headers too large");return;}
        const auto lines=bytes.left(end).split('\n');
        const auto first=lines.value(0).trimmed().split(' ');
        if(first.size()!=3||first[2]!="HTTP/1.1"){respond(socket,400,"Invalid request");return;}
        QByteArray host,origin,contentType;int length=0;bool haveLength=false;
        QSet<QByteArray> seen;
        for(int i=1;i<lines.size();++i){
            const auto line=lines[i].trimmed();const auto colon=line.indexOf(':');
            if(colon<1){respond(socket,400,"Invalid headers");return;}
            const auto name=line.left(colon).toLower();const auto value=line.mid(colon+1).trimmed();
            if(seen.contains(name)){respond(socket,400,"Duplicate header");return;}seen.insert(name);
            if(name=="host")host=value;
            if(name=="origin")origin=value;
            if(name=="content-type")contentType=value;
            if(name=="transfer-encoding"){respond(socket,400,"Unsupported transfer encoding");return;}
            if(name=="content-length"){
                bool ok=false;length=value.toInt(&ok);haveLength=true;
                if(!ok||length<0||length>16384){respond(socket,413,"Invalid length");return;}
            }
        }
        if(host!=QUrl(origin_).authority().toUtf8()){respond(socket,400,"Invalid host");return;}
        const QUrl target=QUrl::fromEncoded(first[1],QUrl::StrictMode);
        if(!target.isValid()||!target.isRelative()||!first[1].startsWith('/')){respond(socket,400,"Invalid target");return;}
        if(first[0]=="POST"&&implicit_&&target.path()=="/oauth-complete/"+state_){
            if(!haveLength||origin!=origin_.toUtf8()||!contentType.startsWith("application/x-www-form-urlencoded")){
                respond(socket,400,"Invalid callback");return;
            }
            if(bytes.size()<end+4+length)return;
            socket->read(end+4+length);
            accept(socket,QUrlQuery(QString::fromUtf8(bytes.mid(end+4,length))));return;
        }
        if(first[0]!="GET"||target.path()!=path_){respond(socket,404,"Not found");return;}
        socket->read(end+4);
        const QUrlQuery query(target);
        if(!implicit_||query.hasQueryItem("error")){accept(socket,query);return;}
        const QByteArray nonce=state_.toUtf8();
        const QByteArray html="<!doctype html><meta charset=utf-8><title>OZK Notify</title><p id=result>Returning authorization to OBS...</p><script nonce='"+nonce+"'>"
            "const p=new URLSearchParams(location.hash.slice(1));history.replaceState(null,'',location.pathname);"
            "fetch('/oauth-complete/"+nonce+"',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})"
            ".then(r=>{document.getElementById('result').textContent=r.ok?'Return to OBS.':'Authorization failed. Return to OBS and try again.'})"
            ".catch(()=>{document.getElementById('result').textContent='Return to OBS to check authorization.'});</script>";
        respond(socket,200,html,"Content-Security-Policy: default-src 'none'; script-src 'nonce-"+nonce+"'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'\r\n");
    }
    QTcpServer server_;QTimer timeout_;QSet<QTcpSocket*> sockets_;
    QString state_,origin_,path_;bool implicit_=true;
};

