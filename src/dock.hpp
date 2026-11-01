#pragma once
#include "feed.hpp"
#include "oauth_loopback.hpp"
#include <QPointer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QStackedWidget;
class QScrollArea;
class QNetworkReply;
class QCheckBox;

class DonationsDock : public QWidget {
    Q_OBJECT
public:
    explicit DonationsDock(QWidget* parent=nullptr);
    void shutdown();
private:
    void showSettings(bool visible);
    void refreshCredentials();
    void render();
    void beginDALogin();
    QString ownPath_;
    QString notifyPath_;
    DonationCredentials credentials_;
    DonationFeed feed_;
    OAuthLoopback login_;
    QStackedWidget* pages_=nullptr;
    QWidget* feedList_=nullptr;
    QVBoxLayout* feedLayout_=nullptr;
    QLabel* total_=nullptr;
    QLabel* daStatus_=nullptr;
    QLabel* dpStatus_=nullptr;
    QLabel* daHint_=nullptr;
    QLabel* dpHint_=nullptr;
    QLabel* settingsMessage_=nullptr;
    QLineEdit* dpKey_=nullptr;
    QCheckBox* daEnabled_=nullptr;
    QCheckBox* dpEnabled_=nullptr;
    QTimer* credentialsTimer_=nullptr;
    QPointer<QNetworkReply> loginReply_;
    bool closing_=false;
};
