#pragma once
#include "feed.hpp"
#include "oauth_loopback.hpp"
#include <QPointer>
#include <QMap>
#include <QThreadPool>
#include <QHash>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QStackedWidget;
class QScrollArea;
class QNetworkReply;
class QCheckBox;
class QNetworkAccessManager;
class YouTubePlayer;

class DonationsDock : public QWidget {
    Q_OBJECT
public:
    explicit DonationsDock(QWidget* parent=nullptr);
    void shutdown();
#ifdef OZK_DONATIONS_PREVIEW
    void previewPopulate();
#endif
private:
    void showSettings(bool visible);
    void refreshCredentials();
    void render();
    void applyCredentials(const DonationCredentials& next);
    QThreadPool credentialsWorker_;
    bool credentialsPending_=false,credentialsAgain_=false,renderPending_=false;
    QByteArray credentialFiles_,listSignature_;
    QHash<QString,QWidget*> rows_;
    void beginDALogin();
    void openVideo(const QString& id);
    void loadRates();
    void fetchRatesOnce();
    QString ownPath_;
    QString notifyPath_;
    DonationCredentials credentials_;
    DonationFeed feed_;
    OAuthLoopback login_;
    QStackedWidget* pages_=nullptr;
    YouTubePlayer* player_=nullptr;
    QWidget* feedList_=nullptr;
    QVBoxLayout* feedLayout_=nullptr;
    QLabel* total_=nullptr;
    QLabel* daTotals_=nullptr;
    QLabel* dpTotals_=nullptr;
    QLabel* combinedUsd_=nullptr;
    QLabel* rateStatus_=nullptr;
    QLabel* daStatus_=nullptr;
    QLabel* dpStatus_=nullptr;
    QLabel* daHint_=nullptr;
    QPushButton* daLogin_=nullptr;
    QLabel* dpHint_=nullptr;
    QLabel* settingsMessage_=nullptr;
    QLineEdit* dpKey_=nullptr;
    QCheckBox* daEnabled_=nullptr;
    QCheckBox* dpEnabled_=nullptr;
    QTimer* credentialsTimer_=nullptr;
    QPointer<QNetworkReply> loginReply_;
    QPointer<QNetworkReply> ratesReply_;
    QNetworkAccessManager* ratesHttp_=nullptr;
    QMap<QString,double> rubPerUnit_;
    QString ratesDate_;
    QString ratesPath_;
    bool closing_=false;
};
