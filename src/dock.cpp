#include "dock.hpp"
#include "credentials.hpp"
#include "youtube_links.hpp"
#include "youtube_player.hpp"
#include <QDesktopServices>
#include <QCheckBox>
#include <QDir>
#include <QFrame>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QLineEdit>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QMap>
#include <QLocale>
#include <cmath>

namespace {
class OzkLogo final : public QWidget {
public:
    OzkLogo(){setFixedSize(28,28);setToolTip("OZK StreamKit");}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#073a2e"));
        p.drawRoundedRect(rect().adjusted(1,1,-1,-1),7,7);
        p.setPen(QPen(QColor("#00e59d"),2.2,Qt::SolidLine,Qt::RoundCap));
        p.setBrush(QColor("#00e59d"));p.drawEllipse(QPointF(14,14),2.6,2.6);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(8,7,12,14),120*16,120*16);
        p.drawArc(QRectF(8,7,12,14),300*16,120*16);
        p.drawArc(QRectF(4,4,20,20),120*16,120*16);
        p.drawArc(QRectF(4,4,20,20),300*16,120*16);
    }
};
constexpr auto dockStyle=R"QSS(
QWidget {background:transparent;color:#eaf2ff;font-family:'Segoe UI';font-size:11px;}
QWidget#donationsRoot {background:#080f19;}
QWidget#homePage, QWidget#settingsPage, QWidget#feedList, QWidget#settingsContent,
QStackedWidget, QScrollArea, QScrollArea QWidget#qt_scrollarea_viewport {background:#080f19;}
QLabel {background:transparent;}
QFrame#header {background:#080f19;border-bottom:1px solid #2c4059;}
QFrame#card {background:#0c1725;border:1px solid #30465e;border-radius:7px;}
QFrame#newDonationCard {background:#151b21;border:1px solid #d9ab3c;border-radius:7px;}
QLabel#title {font-size:16px;font-weight:700;color:#f7fbff;}
QLabel#section {font-size:12px;font-weight:700;color:#eaf2ff;}
QLabel#muted {color:#9db4d6;}
QLabel#number {font-size:19px;font-weight:700;color:#26deb0;}
QLabel#serviceTotal {font-size:11px;color:#a8bfdc;}
QPushButton {background:#192638;color:#dce9fa;border:1px solid #41536d;border-radius:5px;padding:5px 8px;min-height:17px;}
QPushButton:hover {background:#26394d;border-color:#63809e;}
QPushButton#plain {background:transparent;border:0;color:#9db4d6;}
QLineEdit {background:#0b1624;color:#eef6ff;border:1px solid #38506c;border-radius:5px;padding:6px;}
QCheckBox {spacing:6px;color:#c8daf2;}
QScrollArea {border:0;background:transparent;}
)QSS";
QLabel* label(const QString& text,const char* objectName=nullptr) {
    auto* l=new QLabel(text);l->setTextFormat(Qt::PlainText);l->setWordWrap(true);
    if(objectName)l->setObjectName(objectName);
    return l;
}
QFrame* card(QVBoxLayout** inner) {
    auto* frame=new QFrame;frame->setObjectName("card");
    auto* layout=new QVBoxLayout(frame);layout->setContentsMargins(10,9,10,9);layout->setSpacing(4);
    *inner=layout;return frame;
}
QString money(double amount,const QString& currency) {
    return QString("%1 %2").arg(QLocale(QLocale::Russian,QLocale::Russia).toString(amount,'f',
        amount==std::floor(amount)?0:2),currency);
}
QString sums(const QMap<QString,double>& values) {
    QStringList parts;
    for(auto it=values.cbegin();it!=values.cend();++it)parts<<money(it.value(),it.key());
    return parts.isEmpty()?QString::fromUtf8("0 RUB"):parts.join(" · ");
}
QString primarySum(const QMap<QString,double>& values) {
    if(values.isEmpty())return QString::fromUtf8("0 RUB");
    const auto key=values.contains("RUB")?QString("RUB"):values.firstKey();
    return money(values.value(key),key);
}
QString sanitized(QString v,int limit=600) {
    v.remove(QRegularExpression("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F\\x7F]"));
    return v.left(limit).trimmed();
}
}

DonationsDock::DonationsDock(QWidget* parent):QWidget(parent),feed_(this),login_(this) {
    setObjectName("donationsRoot");setStyleSheet(QString::fromUtf8(dockStyle));
    setMinimumWidth(260);ownPath_=ownSettingsPath();notifyPath_=notifySettingsPath();
    feed_.setCachePath(QFileInfo(ownPath_).dir().filePath("donations-cache.bin"));
    auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(0);
    auto* header=new QFrame;header->setObjectName("header");
    auto* headerRow=new QHBoxLayout(header);headerRow->setContentsMargins(8,7,8,7);
    headerRow->addWidget(new OzkLogo);
    headerRow->addWidget(label("OZK Donations","title"));headerRow->addStretch();
    auto* settings=new QPushButton(QString::fromUtf8("⚙"));settings->setToolTip(QString::fromUtf8("Настройки"));
    settings->setFixedSize(28,26);headerRow->addWidget(settings);root->addWidget(header);
    pages_=new QStackedWidget;root->addWidget(pages_,1);
    auto* home=new QWidget;home->setObjectName("homePage");auto* homeLayout=new QVBoxLayout(home);
    homeLayout->setContentsMargins(7,8,7,6);homeLayout->setSpacing(7);
    QVBoxLayout* summaryLayout=nullptr;auto* summary=card(&summaryLayout);
    daTotals_=label({},"serviceTotal");summaryLayout->addWidget(daTotals_);
    dpTotals_=label({},"serviceTotal");summaryLayout->addWidget(dpTotals_);
    total_=label(QString::fromUtf8("Новых донатов пока нет"),"number");summaryLayout->addWidget(total_);
    const auto totalsHint=QString::fromUtf8("История загружается постепенно и сохраняется на этом компьютере. Общий итог показывается после окончания загрузки. Месяц — с 1-го числа по местному времени. Разные валюты не складываются.");
    daTotals_->setToolTip(totalsHint);dpTotals_->setToolTip(totalsHint);
    homeLayout->addWidget(summary);
    auto* heading=label(QString::fromUtf8("Последние донаты"),"section");homeLayout->addWidget(heading);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);
    feedList_=new QWidget;feedList_->setObjectName("feedList");feedLayout_=new QVBoxLayout(feedList_);
    feedLayout_->setContentsMargins(0,0,0,0);feedLayout_->setSpacing(6);
    auto* empty=label(QString::fromUtf8("Ждём первый донат"),"muted");
    empty->setAlignment(Qt::AlignCenter);feedLayout_->addWidget(empty);feedLayout_->addStretch();
    scroll->setWidget(feedList_);homeLayout->addWidget(scroll,1);
    auto* footer=label(QString::fromUtf8("by OTMOPOZOK · v0.4.2"),"muted");
    footer->setAlignment(Qt::AlignCenter);homeLayout->addWidget(footer);
    pages_->addWidget(home);

    auto* settingsPage=new QWidget;settingsPage->setObjectName("settingsPage");auto* settingsOuter=new QVBoxLayout(settingsPage);
    settingsOuter->setContentsMargins(7,8,7,7);settingsOuter->setSpacing(8);
    auto* back=new QPushButton(QString::fromUtf8("← Назад к донатам"));
    settingsOuter->addWidget(back);
    auto* settingsScroll=new QScrollArea;settingsScroll->setWidgetResizable(true);
    auto* settingsContent=new QWidget;settingsContent->setObjectName("settingsContent");auto* settingsLayout=new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0,0,0,0);settingsLayout->setSpacing(8);
    QVBoxLayout* daLayout=nullptr;settingsLayout->addWidget(card(&daLayout));
    daLayout->addWidget(label("DonationAlerts","section"));
    daEnabled_=new QCheckBox(QString::fromUtf8("Показывать донаты DonationAlerts"));
    daLayout->addWidget(daEnabled_);
    daStatus_=label({},"muted");daLayout->addWidget(daStatus_);
    daHint_=label(QString::fromUtf8("Если аккаунт уже подключён в OZK Notify, новые донаты видны без повторного входа. Для истории нужно отдельно разрешить чтение донатов."),"muted");
    daLayout->addWidget(daHint_);
    daLogin_=new QPushButton(QString::fromUtf8("Разрешить историю DonationAlerts"));daLayout->addWidget(daLogin_);
    QVBoxLayout* dpLayout=nullptr;settingsLayout->addWidget(card(&dpLayout));
    dpLayout->addWidget(label("DonatePay","section"));
    dpEnabled_=new QCheckBox(QString::fromUtf8("Показывать донаты DonatePay"));
    dpLayout->addWidget(dpEnabled_);
    dpStatus_=label({},"muted");dpLayout->addWidget(dpStatus_);
    dpHint_=label(QString::fromUtf8("Если DonatePay подключён в OZK Notify, ключ вводить повторно не нужно."),"muted");
    dpLayout->addWidget(dpHint_);
    dpKey_=new QLineEdit;dpKey_->setEchoMode(QLineEdit::Password);
    dpKey_->setPlaceholderText(QString::fromUtf8("Ключ API DonatePay"));dpLayout->addWidget(dpKey_);
    auto* saveDP=new QPushButton(QString::fromUtf8("Сохранить ключ"));dpLayout->addWidget(saveDP);
    auto* dpHelp=new QPushButton(QString::fromUtf8("Где взять ключ DonatePay ↗"));dpLayout->addWidget(dpHelp);
    settingsMessage_=label({},"muted");settingsLayout->addWidget(settingsMessage_);
    settingsLayout->addStretch();settingsScroll->setWidget(settingsContent);
    settingsOuter->addWidget(settingsScroll,1);pages_->addWidget(settingsPage);
    player_=new YouTubePlayer;pages_->addWidget(player_);
    connect(player_,&YouTubePlayer::returnRequested,this,[this]{pages_->setCurrentIndex(0);});
    connect(settings,&QPushButton::clicked,this,[this]{showSettings(true);});
    connect(back,&QPushButton::clicked,this,[this]{showSettings(false);});
    connect(daLogin_,&QPushButton::clicked,this,&DonationsDock::beginDALogin);
    connect(saveDP,&QPushButton::clicked,this,[this]{
        const QString key=dpKey_->text().trimmed();
        if(key.isEmpty()){settingsMessage_->setText(QString::fromUtf8("Введите ключ DonatePay."));return;}
        const auto own=loadCredentials(ownPath_,{});
        if(!saveOwnCredentials(ownPath_,own.da,key)){
            settingsMessage_->setText(QString::fromUtf8("Не удалось сохранить ключ."));return;
        }
        dpKey_->clear();settingsMessage_->setText(QString::fromUtf8("Ключ сохранён на этом компьютере."));
        refreshCredentials();
    });
    connect(dpHelp,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("https://donatepay.ru/page/api"));});
    auto saveChoices=[this]{
        if(saveServiceChoices(ownPath_,daEnabled_->isChecked(),dpEnabled_->isChecked()))refreshCredentials();
        else settingsMessage_->setText(QString::fromUtf8("Не удалось сохранить выбор сервисов."));
    };
    connect(daEnabled_,&QCheckBox::toggled,this,saveChoices);
    connect(dpEnabled_,&QCheckBox::toggled,this,saveChoices);
    connect(&feed_,&DonationFeed::updated,this,&DonationsDock::render);
    connect(&login_,&OAuthLoopback::failed,this,[this](const QString&){
        settingsMessage_->setText(QString::fromUtf8("Вход не завершён. Попробуйте ещё раз."));
    });
    connect(&login_,&OAuthLoopback::received,this,[this](const QUrlQuery& query){
        const QString token=query.queryItemValue("access_token",QUrl::FullyDecoded);
        static const QRegularExpression valid("^[A-Za-z0-9._~+/-]+=*$");
        if(!valid.match(token).hasMatch()||query.queryItemValue("token_type").compare("bearer",Qt::CaseInsensitive)!=0){
            settingsMessage_->setText(QString::fromUtf8("DonationAlerts не подтвердил вход."));return;
        }
        auto* http=new QNetworkAccessManager(this);
        QNetworkRequest req(QUrl("https://www.donationalerts.com/api/v1/user/oauth"));
        req.setRawHeader("Authorization",("Bearer "+token).toUtf8());req.setTransferTimeout(10000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        auto* reply=http->get(req);loginReply_=reply;
        connect(reply,&QNetworkReply::finished,this,[this,reply,http,token]{
            const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto user=QJsonDocument::fromJson(reply->readAll()).object().value("data").toObject();
            reply->deleteLater();http->deleteLater();loginReply_=nullptr;
            if(closing_)return;
            if(code!=200||user.value("id").isUndefined()||user.value("socket_connection_token").toString().isEmpty()){
                settingsMessage_->setText(QString::fromUtf8("Не удалось проверить доступ DonationAlerts."));return;
            }
            const auto own=loadCredentials(ownPath_,{});
            if(!saveOwnCredentials(ownPath_,token,own.dp)){
                settingsMessage_->setText(QString::fromUtf8("Не удалось сохранить вход."));return;
            }
            settingsMessage_->setText(QString::fromUtf8("DonationAlerts подключён."));refreshCredentials();
        });
    });
    credentialsTimer_=new QTimer(this);credentialsTimer_->setInterval(15000);
    connect(credentialsTimer_,&QTimer::timeout,this,&DonationsDock::refreshCredentials);
    credentialsTimer_->start();refreshCredentials();
}
void DonationsDock::showSettings(bool visible) {
    if(pages_->currentWidget()==player_)player_->stop();
    pages_->setCurrentIndex(visible?1:0);
}
void DonationsDock::openVideo(const QString& id) {
    if(player_->play(id))pages_->setCurrentWidget(player_);
}
void DonationsDock::refreshCredentials() {
    if(closing_)return;
    const auto next=loadCredentials(ownPath_,notifyPath_);
    if(next.da!=credentials_.da||next.dp!=credentials_.dp||
       next.daEnabled!=credentials_.daEnabled||next.dpEnabled!=credentials_.dpEnabled){
        credentials_=next;DonationCredentials active=credentials_;
        if(!active.daEnabled)active.da.clear();if(!active.dpEnabled)active.dp.clear();
        feed_.configure(active);
    }
    else credentials_=next;
    daEnabled_->blockSignals(true);dpEnabled_->blockSignals(true);
    daEnabled_->setChecked(credentials_.daEnabled);dpEnabled_->setChecked(credentials_.dpEnabled);
    daEnabled_->blockSignals(false);dpEnabled_->blockSignals(false);
    render();
}
void DonationsDock::beginDALogin() {
    // The redirect registered for the existing OZK Notify DonationAlerts app.
    if(!login_.begin(38561,"/donationalerts",true)){
        settingsMessage_->setText(QString::fromUtf8("Не удалось открыть локальный вход. Закройте другое окно авторизации."));return;
    }
    QUrl url("https://www.donationalerts.com/oauth/authorize");QUrlQuery query;
    query.addQueryItem("client_id","22338");query.addQueryItem("redirect_uri",login_.redirect().toString());
    query.addQueryItem("response_type","token");
    query.addQueryItem("scope","oauth-user-show oauth-donation-subscribe oauth-donation-index");
    query.addQueryItem("state",login_.state());url.setQuery(query);
    settingsMessage_->setText(QString::fromUtf8("Подтвердите вход в открывшемся браузере."));
    if(!QDesktopServices::openUrl(url)){
        login_.stop();settingsMessage_->setText(QString::fromUtf8("Браузер не открылся."));
    }
}
void DonationsDock::render() {
    if(closing_)return;
    daStatus_->setText(QString::fromUtf8("Состояние: ")+(credentials_.daEnabled?feed_.daStatus():QString::fromUtf8("Выключен"))+
        (credentials_.daFromNotify?QString::fromUtf8(" · вход через OZK Notify"):QString()));
    if(feed_.daHistoryPermissionNeeded()&&credentials_.daEnabled)
        daHint_->setText(QString::fromUtf8("Новые донаты приходят через OZK Notify. Для показа прошлых донатов нажмите «Разрешить историю DonationAlerts» и подтвердите доступ один раз."));
    else if(credentials_.da.isEmpty())
        daHint_->setText(QString::fromUtf8("Подключите DonationAlerts, чтобы видеть новые и прошлые донаты в одной ленте."));
    else if(!credentials_.daFromNotify&&!credentials_.da.isEmpty())
        daHint_->setText(QString::fromUtf8("История и новые донаты доступны через этот вход. OZK Notify можно использовать отдельно."));
    else daHint_->setText(QString::fromUtf8("Новые донаты из OZK Notify видны без повторного входа. Для прошлых донатов нужен отдельный доступ."));
    daLogin_->setText(credentials_.da.isEmpty()?QString::fromUtf8("Подключить DonationAlerts"):
                      QString::fromUtf8("Разрешить историю DonationAlerts"));
    dpStatus_->setText(QString::fromUtf8("Состояние: ")+(credentials_.dpEnabled?feed_.dpStatus():QString::fromUtf8("Выключен"))+
        (credentials_.dpFromNotify?QString::fromUtf8(" · ключ из OZK Notify"):QString()));
    // A connected account shows the feed directly. No connection cards on the main page.
    QVector<DonationEntry> entries;
    for(const auto& entry:feed_.entries())
        if((entry.service=="DonationAlerts"&&credentials_.daEnabled)||
           (entry.service=="DonatePay"&&credentials_.dpEnabled))entries.append(entry);
    while(feedLayout_->count()>0){auto* item=feedLayout_->takeAt(0);delete item->widget();delete item;}
    if(feed_.daHistoryPermissionNeeded()&&credentials_.daEnabled){
        auto* hint=label(QString::fromUtf8("История DonationAlerts пока не разрешена. Откройте ⚙ и нажмите «Разрешить историю DonationAlerts»."),"muted");
        feedLayout_->addWidget(hint);
    }
    auto serviceTotal=[&](const QString& service){
        const auto totals=feed_.totals(service);
        if(!totals.complete)return QString::fromUtf8("%1: считаем всю историю…").arg(service);
        const auto other=totals.all.size()>1?QString::fromUtf8(" · ещё %1 валют").arg(totals.all.size()-1):QString();
        return QString::fromUtf8("%1: %2 всего · %3 за месяц%4")
            .arg(service,primarySum(totals.all),primarySum(totals.month),other);
    };
    daTotals_->setVisible(credentials_.daEnabled);dpTotals_->setVisible(credentials_.dpEnabled);
    daTotals_->setText(serviceTotal("DonationAlerts"));dpTotals_->setText(serviceTotal("DonatePay"));
    const auto detail=[&](const QString& service){
        const auto totals=feed_.totals(service);
        if(!totals.complete)return QString::fromUtf8("Подсчёт истории продолжается. Результат сохраняется после каждой страницы.");
        return QString::fromUtf8("Всего: %1\nЗа месяц: %2").arg(sums(totals.all),sums(totals.month));
    };
    daTotals_->setToolTip(detail("DonationAlerts"));dpTotals_->setToolTip(detail("DonatePay"));
    if(entries.isEmpty()){
        const QString message=feed_.loading()?QString::fromUtf8("Подгружаем последние донаты…"):
             feed_.hasAccounts()?QString::fromUtf8("Последних донатов пока нет"):
             QString::fromUtf8("Подключите DonationAlerts или DonatePay в настройках ⚙");
        auto* placeholder=label(message,"muted");placeholder->setAlignment(Qt::AlignCenter);
        feedLayout_->addWidget(placeholder);
        total_->setText(QString::fromUtf8("Новых донатов пока нет"));
    } else {
        QMap<QString,double> totals;int sessionCount=0;
        for(const auto& entry:entries)if(entry.session){totals[entry.currency]+=entry.amount;++sessionCount;}
        total_->setText(sessionCount?QString::fromUtf8("Новые донаты: %1 · %2").arg(sessionCount).arg(sums(totals)):
            QString::fromUtf8("Новых донатов пока нет"));
        for(const auto& entry:entries){
            QVBoxLayout* inside=nullptr;auto* row=card(&inside);
            if(entry.session)row->setObjectName("newDonationCard");
            auto* top=new QHBoxLayout;top->setSpacing(5);
            auto* logo=new QLabel;logo->setPixmap(QPixmap(entry.service=="DonationAlerts"?
                ":/ozk-donations/brands/donationalerts.png":":/ozk-donations/brands/donatepay.png")
                .scaled(19,19,Qt::KeepAspectRatio,Qt::SmoothTransformation));
            top->addWidget(logo);
            auto* name=label(sanitized(entry.donor.isEmpty()?QString::fromUtf8("Анонимно"):entry.donor,80),"section");
            top->addWidget(name,1);
            auto* sum=label(money(entry.amount,entry.currency));
            sum->setStyleSheet("color:#20deb2;font-weight:700;");top->addWidget(sum);
            inside->addLayout(top);
            auto* source=label(entry.service+" · "+entry.at.toString("dd.MM · HH:mm"),"muted");inside->addWidget(source);
            if(!entry.message.trimmed().isEmpty()){
                const QString message=sanitized(entry.message);
                auto* messageLabel=label(message);
                const QString linked=donationTextWithYouTubeLinks(message);
                if(!linked.isEmpty()){
                    messageLabel->setTextFormat(Qt::RichText);
                    messageLabel->setText(linked);
                    messageLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
                    messageLabel->setOpenExternalLinks(false);
                    connect(messageLabel,&QLabel::linkActivated,this,[this](const QString& href){
                        if(href.startsWith("ozkvideo:"))openVideo(href.mid(9));
                    });
                }
                inside->addWidget(messageLabel);
            }
            feedLayout_->addWidget(row);
        }
    }
    feedLayout_->addStretch();
}
void DonationsDock::shutdown() {
    if(closing_)return;
    closing_=true;credentialsTimer_->stop();login_.stop();
    if(loginReply_)loginReply_->abort();player_->stop();feed_.stop();
}
