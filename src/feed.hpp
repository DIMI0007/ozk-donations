#pragma once
#include "credentials.hpp"
#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>
#include <QThreadPool>
#include <QVector>
#include <QWebSocket>

struct DonationEntry {
    QString service,id,donor,message,currency;
    double amount=0;
    QDateTime at;
    bool valid() const {return !service.isEmpty()&&!id.isEmpty()&&amount>0&&
                        !currency.isEmpty();}
    bool session=true;
};
DonationEntry parseDA(const QJsonObject& packet);
DonationEntry parseDP(const QJsonObject& packet);
bool occurredThisSession(const DonationEntry& entry,const QDateTime& startedAt);

class DonationFeed : public QObject {
    Q_OBJECT
public:
    explicit DonationFeed(QObject* parent=nullptr);
    ~DonationFeed() override;
    void configure(const DonationCredentials& c);
    void setCachePath(const QString& path) { cachePath_=path; }
    void stop();
    const QVector<DonationEntry>& entries() const {return entries_;}
    QString daStatus() const {return daStatus_;}
    QString dpStatus() const {return dpStatus_;}
    bool daHistoryPermissionNeeded() const {return daHistoryPermissionNeeded_;}
    bool loading() const {return hasAccounts()&&
        ((!daToken_.isEmpty()&&daStatus_.contains(QString::fromUtf8("подключение"),Qt::CaseInsensitive))||
         (!dpToken_.isEmpty()&&dpStatus_.contains(QString::fromUtf8("подключение"),Qt::CaseInsensitive)));}
    bool hasAccounts() const {return !daToken_.isEmpty()||!dpToken_.isEmpty();}
    struct Totals {QMap<QString,double> all,month;bool complete=false;};
    Totals totals(const QString& service) const {return service=="DonationAlerts"?daTotals_:dpTotals_;}
#ifdef OZK_DONATIONS_PREVIEW
    void previewPopulate() {
        const auto now=QDateTime::currentDateTime();
        entries_={
            {"DonationAlerts","preview-1",QString::fromUtf8("Зритель"),QString::fromUtf8("Спасибо за эфир!"),"RUB",300,now.addSecs(-120),true},
            {"DonatePay","preview-2",QString::fromUtf8("Гость"),QString::fromUtf8("Музыка: https://youtu.be/AbCdEfGh123"),"RUB",100,now.addSecs(-3600),false},
            {"DonationAlerts","preview-3",QString::fromUtf8("Подписчик"),QString::fromUtf8("Хорошего стрима!"),"USD",5,now.addDays(-1),false}
        };
        daTotals_.all={{"RUB",5000},{"USD",5}};daTotals_.month={{"RUB",1200},{"USD",5}};daTotals_.complete=true;
        dpTotals_.all={{"RUB",3200}};dpTotals_.month={{"RUB",700}};dpTotals_.complete=true;
        Q_EMIT updated();
    }
#endif
Q_SIGNALS:
    void updated();
private:
    void connectDA();
    void subscribeDA(const QString& client);
    void receiveDA(const QString& text);
    void retryDA();
    void fetchDAHistory();
    void fetchDAHistoryPage(int page);
    void pollDP();
    void fetchDPHistory(int skip);
    void collectTotal(const DonationEntry& entry);
    void updateDataUsage();
    void loadTotalsCache();
    void saveTotalsCache();
    void saveCache();
    void add(const DonationEntry& entry);
    void setStatus(QString& target,const QString& status);
    QNetworkAccessManager http_;
    QWebSocket socket_;
    QTimer daRetry_,dpTimer_,cacheTimer_,totalsCacheTimer_;
    QThreadPool cacheWorker_;
    void writeTotalsCache();
    QString daToken_,dpToken_,daChannel_,daSocketToken_;
    QString daStatus_=QString::fromUtf8("Не подключён");
    QString dpStatus_=QString::fromUtf8("Не подключён");
    QVector<DonationEntry> entries_;
    QSet<QString> seen_;
    quint64 daEpoch_=0,dpEpoch_=0;
    int daFailures_=0,dpFailures_=0;
    bool closing_=false,resettingSocket_=false;
    QDateTime startedAt_=QDateTime::currentDateTimeUtc();
    bool daHistoryPermissionNeeded_=false;
    bool daHistoryChecked_=false;
    Totals daTotals_,dpTotals_;
    QSet<QString> daTotalIds_,dpTotalIds_;
    bool daTotalsStarted_=false,dpTotalsStarted_=false;
    int daNextPage_=1,dpNextSkip_=0;
    QString cachePath_;
};
