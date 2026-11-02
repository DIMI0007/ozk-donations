#pragma once
#include <QWidget>
#include <QTcpServer>
#include <QSet>

class QCefWidget;
class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QTcpSocket;

// OBS Browser is created only while the user watches a requested clip.
class YouTubePlayer final : public QWidget {
    Q_OBJECT
public:
    explicit YouTubePlayer(QWidget* parent=nullptr);
    bool play(const QString& videoId);
    void stop();
Q_SIGNALS:
    void returnRequested();
private:
    void serve(QTcpSocket* socket,const QByteArray& request);
    QByteArray page() const;
    void setStatus(const QString& text);
    void resizeEvent(QResizeEvent* event) override;
    QTcpServer server_;
    QSet<QTcpSocket*> sockets_;
    QFrame* videoFrame_=nullptr;
    QVBoxLayout* videoLayout_=nullptr;
    QLabel* status_=nullptr;
    QPushButton* external_=nullptr;
    QCefWidget* browser_=nullptr;
    QString videoId_,nonce_;
};
