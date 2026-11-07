#include "ozk_performance.hpp"
#include "dock.hpp"
#include "credentials.hpp"
#include "youtube_links.hpp"
#include "youtube_player.hpp"
#include "telemetry.hpp"
#include "currency_conversion.hpp"
#include <QDesktopServices>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QIcon>
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
#include <QSaveFile>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <cmath>
#include <QDataStream>
#include <QScrollBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QUuid>
#include <QMessageBox>

namespace {
QIcon settingsIcon() {
    QPixmap image(48,48);image.fill(Qt::transparent);
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.scale(2,2);
    painter.setPen(QPen(QColor("#aebde3"),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);painter.translate(12,12);
    for(int i=0;i<8;++i){painter.drawLine(QPointF(0,-8),QPointF(0,-10));painter.rotate(45);}
    painter.drawEllipse(QPointF(0,0),6.8,6.8);painter.drawEllipse(QPointF(0,0),2.5,2.5);
    painter.end();image.setDevicePixelRatio(2);return QIcon(image);
}
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
class CoinIcon final : public QWidget {
public:
    CoinIcon(){setFixedSize(18,18);setToolTip(QString::fromUtf8("Донаты"));}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#f5bd32"),1.6));p.setBrush(QColor("#5a3d0e"));
        p.drawEllipse(QRectF(2,2,14,14));
        p.setPen(QPen(QColor("#ffe17b"),1.5,Qt::SolidLine,Qt::RoundCap));
        p.drawArc(QRectF(5,5,8,8),75*16,215*16);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#ffd34a"));p.drawEllipse(QRectF(8,7,2,4));
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
QLabel#title {font-size:17px;font-weight:700;color:#e7fff4;}
QLabel#section {font-size:12px;font-weight:700;color:#eaf2ff;}
QLabel#muted {color:#9db4d6;}
QLabel#number {font-size:16px;font-weight:700;color:#26deb0;}
QLabel#serviceTotal,QLabel#otherServiceTotal {font-size:11px;color:#a8bfdc;}
QFrame#usdBox {background:#1a1b17;border:1px solid #796130;border-radius:6px;}
QLabel#usdNumber,QLabel#combinedUsdTotal {font-size:15px;font-weight:700;color:#ffd34a;}
QPushButton {background:#192638;color:#dce9fa;border:1px solid #41536d;border-radius:5px;padding:5px 8px;min-height:17px;}
QPushButton:hover {background:#26394d;border-color:#63809e;}
QPushButton#plain {background:transparent;border:0;color:#9db4d6;}
QPushButton#SettingsShortcut{color:#e1ebff;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #202a3b,stop:1 #141d2b);border:1px solid #39465d;border-radius:8px;padding:0;}
QWidget#settingsPage,QWidget#settingsContent{background:#080f18;color:#dfe8ff;font-size:12px;}
QWidget#settingsPage QFrame#card{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #101925,stop:1 #0b131e);border:1px solid #263244;border-radius:8px;}
QWidget#settingsPage QLabel#section{color:#edf3ff;font-size:13px;font-weight:700;}
QWidget#settingsPage QLabel#muted{color:#8ea1c0;font-size:11px;}
QWidget#settingsPage QPushButton{color:#e1ebff;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #202a3b,stop:1 #141d2b);border:1px solid #39465d;border-radius:8px;min-height:20px;padding:3px 7px;font-size:12px;}
QWidget#settingsPage QPushButton:hover{background:#26344d;border-color:#6476a0;}
QWidget#settingsPage QPushButton:focus{border-color:#8a79ff;}
QWidget#settingsPage QLineEdit{color:#e7eeff;background:#0c1420;border:1px solid #344865;border-radius:6px;padding:4px;font-size:12px;}
QWidget#settingsPage QLineEdit:focus{border-color:#776af5;background:#111c2e;}
QWidget#settingsPage QCheckBox{color:#dce7fb;spacing:6px;min-height:20px;font-size:12px;}
QWidget#settingsPage QCheckBox::indicator{width:16px;height:16px;}
QWidget#settingsPage QCheckBox::indicator:unchecked{border-radius:4px;background:#344357;border:1px solid #65748d;}
QWidget#settingsPage QCheckBox::indicator:checked{border-radius:4px;background:#00d37d;border:1px solid #2ff9a0;}
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
    auto* frame=new QFrame;frame->setObjectName("card");frame->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
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
QString compactRub(double amount,bool converted) {
    return (converted?QString::fromUtf8("≈ "):QString())+
        QLocale(QLocale::Russian,QLocale::Russia).toString(amount,'f',0)+QString::fromUtf8(" ₽");
}
QString sanitized(QString v,int limit=600) {
    v.remove(QRegularExpression("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F\\x7F]"));
    return v.left(limit).trimmed();
}
}

DonationsDock::DonationsDock(QWidget* parent):QWidget(parent),feed_(this),login_(this) {
    ozkperf::install();
    setObjectName("donationsRoot");setStyleSheet(QString::fromUtf8(dockStyle));
    setMinimumWidth(260);ownPath_=ownSettingsPath();notifyPath_=notifySettingsPath();
    ratesPath_=QFileInfo(ownPath_).dir().filePath("exchange-rates.json");
    feed_.setCachePath(QFileInfo(ownPath_).dir().filePath("donations-cache.bin"));
    auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(0);
    auto* header=new QFrame;header->setObjectName("header");
    auto* headerRow=new QHBoxLayout(header);headerRow->setContentsMargins(10,10,10,7);headerRow->setSpacing(7);
    headerRow->addWidget(new OzkLogo);
    auto* headerTitle=label("<span style='color:#57dfd1'>OZK</span> Donations","title");
    headerTitle->setTextFormat(Qt::RichText);headerTitle->setAccessibleName("OZK Donations");
    headerRow->addWidget(headerTitle);headerRow->addStretch();
    auto* settings=new QPushButton;settings->setObjectName("SettingsShortcut");
    settings->setIcon(settingsIcon());settings->setIconSize(QSize(19,19));
    settings->setToolTip(QString::fromUtf8("Настройки"));settings->setAccessibleName(settings->toolTip());
    settings->setFixedSize(30,28);headerRow->addWidget(settings);root->addWidget(header);
    pages_=new QStackedWidget;root->addWidget(pages_,1);
    auto* home=new QWidget;home->setObjectName("homePage");auto* homeLayout=new QVBoxLayout(home);
    homeLayout->setContentsMargins(7,8,7,6);homeLayout->setSpacing(7);
    QVBoxLayout* summaryLayout=nullptr;auto* summary=card(&summaryLayout);
    summaryLayout->setContentsMargins(9,6,9,7);summaryLayout->setSpacing(3);
    auto* summaryTitle=new QHBoxLayout;summaryTitle->setSpacing(4);
    summaryTitle->addWidget(new CoinIcon);summaryTitle->addWidget(label(QString::fromUtf8("Итоги донатов"),"section"));
    summaryTitle->addStretch();
    auto* usdBox=new QFrame;usdBox->setObjectName("usdBox");usdBox->setFixedWidth(104);
    auto* usdLayout=new QVBoxLayout(usdBox);usdLayout->setContentsMargins(6,3,6,3);usdLayout->setSpacing(1);
    usdLayout->addWidget(label(QString::fromUtf8("ВСЕГО"),"muted"));
    combinedUsd_=label(QString::fromUtf8("—"),"usdNumber");combinedUsd_->setObjectName("combinedUsdTotal");usdLayout->addWidget(combinedUsd_);
    summaryTitle->addWidget(usdBox);summaryLayout->addLayout(summaryTitle);
    daTotals_=label({},"serviceTotal");daTotals_->setWordWrap(false);summaryLayout->addWidget(daTotals_);
    dpTotals_=label({},"serviceTotal");dpTotals_->setWordWrap(false);summaryLayout->addWidget(dpTotals_);
    otherTotals_=label({},"serviceTotal");otherTotals_->setObjectName("otherServiceTotal");summaryLayout->addWidget(otherTotals_);
    otherTotals_->setToolTip(QString::fromUtf8("Сумма за всё время, добавленная вручную. Учтена в общем итоге в долларах, но не в новых донатах или итогах за месяц."));
    total_=label(QString::fromUtf8("Новых донатов пока нет"),"number");summaryLayout->addWidget(total_);
    const auto totalsHint=QString::fromUtf8("История загружается постепенно и сохраняется на этом компьютере. Общий итог показывается после окончания загрузки. Месяц — с 1-го числа по местному времени. Разные валюты не складываются.");
    daTotals_->setToolTip(totalsHint);dpTotals_->setToolTip(totalsHint);
    homeLayout->addWidget(summary);
    auto* headingRow=new QHBoxLayout;headingRow->setSpacing(4);
    headingRow->addWidget(new CoinIcon);headingRow->addWidget(label(QString::fromUtf8("Последние донаты"),"section"));
    headingRow->addStretch();homeLayout->addLayout(headingRow);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);
    feedList_=new QWidget;feedList_->setObjectName("feedList");feedLayout_=new QVBoxLayout(feedList_);
    feedLayout_->setContentsMargins(0,0,0,0);feedLayout_->setSpacing(6);
    auto* empty=label(QString::fromUtf8("Ждём первый донат"),"muted");
    empty->setAlignment(Qt::AlignCenter);feedLayout_->addWidget(empty);feedLayout_->addStretch();
    scroll->setWidget(feedList_);homeLayout->addWidget(scroll,1);
    auto* footer=label(QString::fromUtf8("by OTMOPOZOK · v0.6.4"),"muted");
    footer->setAlignment(Qt::AlignCenter);homeLayout->addWidget(footer);
    pages_->addWidget(home);

    auto* settingsPage=new QWidget;settingsPage->setObjectName("settingsPage");auto* settingsOuter=new QVBoxLayout(settingsPage);
    settingsOuter->setContentsMargins(8,8,8,8);settingsOuter->setSpacing(7);
    auto* settingsHeader=new QHBoxLayout;settingsHeader->setSpacing(7);
    auto* back=new QPushButton(QString::fromUtf8("← Назад"));back->setObjectName("settingsBack");back->setFixedWidth(70);
    settingsHeader->addWidget(back);
    auto* settingsTitle=label(QString::fromUtf8("Настройки"),"title");settingsHeader->addWidget(settingsTitle,1);
    settingsOuter->addLayout(settingsHeader);
    auto* settingsScroll=new QScrollArea;settingsScroll->setWidgetResizable(true);
    auto* settingsContent=new QWidget;settingsContent->setObjectName("settingsContent");auto* settingsLayout=new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0,0,0,0);settingsLayout->setSpacing(7);settingsLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
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
    auto* dpActions=new QHBoxLayout;dpActions->setSpacing(4);
    auto* saveDP=new QPushButton(QString::fromUtf8("Сохранить ключ"));dpActions->addWidget(saveDP,1);
    auto* dpHelp=new QPushButton(QString::fromUtf8("Где взять ключ?"));dpHelp->setToolTip(QString::fromUtf8("Где взять ключ DonatePay"));dpActions->addWidget(dpHelp,1);
    dpLayout->addLayout(dpActions);
    QVBoxLayout* manualLayout=nullptr;manualBox_=card(&manualLayout);settingsLayout->addWidget(manualBox_);
    auto* manualHeader=new QHBoxLayout;manualHeader->addWidget(label(QString::fromUtf8("Донаты от других сервисов"),"section"),1);
    auto* addManual=new QPushButton("+");addManual->setObjectName("addManualService");addManual->setFixedSize(30,28);
    addManual->setToolTip(QString::fromUtf8("Добавить сервис вручную"));manualHeader->addWidget(addManual);manualLayout->addLayout(manualHeader);
    manualLayout->addWidget(label(QString::fromUtf8("Ручной ввод общей суммы за всё время в рублях. Эти суммы войдут в общий итог в долларах по сохранённому курсу."),"muted"));
    manualList_=new QVBoxLayout;manualList_->setSpacing(4);manualLayout->addLayout(manualList_);
    connect(addManual,&QPushButton::clicked,this,[this]{editManualService();});
    QVBoxLayout* ratesLayout=nullptr;settingsLayout->addWidget(card(&ratesLayout));
    ratesLayout->addWidget(label(QString::fromUtf8("Курс для итогов"),"section"));
    ratesLayout->addWidget(label(QString::fromUtf8("Один сохранённый курс переводит иностранные донаты в рубли и общий итог в доллары. Это приблизительная оценка, а не сумма выплат."),"muted"));
    rateStatus_=label({},"muted");ratesLayout->addWidget(rateStatus_);
    auto* refreshRates=new QPushButton(QString::fromUtf8("Обновить курс один раз"));ratesLayout->addWidget(refreshRates);
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
    connect(refreshRates,&QPushButton::clicked,this,&DonationsDock::fetchRatesOnce);
    auto saveChoices=[this]{
        if(saveServiceChoices(ownPath_,daEnabled_->isChecked(),dpEnabled_->isChecked()))refreshCredentials();
        else settingsMessage_->setText(QString::fromUtf8("Не удалось сохранить выбор сервисов."));
    };
    connect(daEnabled_,&QCheckBox::toggled,this,saveChoices);
    connect(dpEnabled_,&QCheckBox::toggled,this,saveChoices);
    connect(&feed_,&DonationFeed::updated,this,[this]{
        if(renderPending_||closing_)return;renderPending_=true;
        QTimer::singleShot(0,this,[this]{renderPending_=false;render();});
    });
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
    credentialsTimer_->start();loadRates();
#ifndef OZK_DONATIONS_PREVIEW
    if(!QFileInfo::exists(ratesPath_))fetchRatesOnce();
#endif
    credentialsWorker_.setMaxThreadCount(1);
    manualPath_=QFileInfo(ownPath_).dir().filePath("manual-services.json");rebuildManualServices();
#ifndef OZK_DONATIONS_PREVIEW
    const auto manualPath=manualPath_;
    credentialsWorker_.start([this,manualPath]{const auto rows=loadManualServices(manualPath);
        QMetaObject::invokeMethod(this,[this,rows]{if(closing_)return;manualServices_=rows;rebuildManualServices();render();},Qt::QueuedConnection);
    });
#endif
#ifndef OZK_DONATIONS_PREVIEW
    refreshCredentials();
#endif
}
#ifdef OZK_DONATIONS_PREVIEW
void DonationsDock::previewPopulate() {
    rubPerUnit_.insert("USD",90.0);
    ratesDate_=QDate::currentDate().toString(Qt::ISODate);
    feed_.previewPopulate();
}
#endif
void DonationsDock::showSettings(bool visible) {
    if(pages_->currentWidget()==player_)player_->stop();
    pages_->setCurrentIndex(visible?1:0);
}
void DonationsDock::openVideo(const QString& id) {
    if(player_->play(id))pages_->setCurrentWidget(player_);
}
void DonationsDock::loadRates() {
    donations_telemetry::CpuScope cpu;
    rubPerUnit_.clear();ratesDate_.clear();rubPerUnit_.insert("RUB",1.0);
    QString source=ratesPath_;
    if(!QFileInfo::exists(source))source=QFileInfo(notifyPath_).dir().filePath("exchange-rates.json");
    QFile file(source);if(!file.open(QIODevice::ReadOnly)||file.size()>128*1024)return;
    const auto json=QJsonDocument::fromJson(file.readAll()).object();
    const auto rates=json.value("rub_per_unit").toObject();
    for(auto it=rates.begin();it!=rates.end();++it){
        const double value=it.value().toDouble();
        if(it.key().size()==3&&std::isfinite(value)&&value>0)rubPerUnit_.insert(it.key().toUpper(),value);
    }
    ratesDate_=json.value("date").toString();
}
void DonationsDock::fetchRatesOnce() {
    donations_telemetry::CpuScope cpu;
    if(closing_||ratesReply_)return;
    ratesHttp_=new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl("https://www.cbr-xml-daily.ru/daily_json.js"));
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    auto* reply=donations_telemetry::watch(ratesHttp_->get(req));ratesReply_=reply;
    render();
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        donations_telemetry::CpuScope cpu;
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto json=QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();ratesReply_=nullptr;
        if(closing_)return;
        if(code!=200||json.value("Date").toString().isEmpty()){render();return;}
        QJsonObject rates{{"RUB",1.0}};
        const auto rows=json.value("Valute").toObject();
        for(auto it=rows.begin();it!=rows.end();++it){
            const auto row=it.value().toObject();
            const auto code=row.value("CharCode").toString().toUpper();
            const double nominal=row.value("Nominal").toDouble();
            const double value=row.value("Value").toDouble();
            if(code.size()==3&&std::isfinite(nominal)&&nominal>0&&std::isfinite(value)&&value>0)
                rates.insert(code,value/nominal);
        }
        if(rates.size()<3||!QDir().mkpath(QFileInfo(ratesPath_).absolutePath())){render();return;}
        QSaveFile file(ratesPath_);
        const auto bytes=QJsonDocument(QJsonObject{{"source","https://www.cbr-xml-daily.ru/daily_json.js"},
            {"date",json.value("Date")},{"rub_per_unit",rates}}).toJson();
        if(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit()){
            loadRates();render();
        } else render();
    });
}
void DonationsDock::refreshCredentials() {
    donations_telemetry::CpuScope cpu;
    if(closing_)return;
    if(credentialsPending_){credentialsAgain_=true;return;}
    credentialsPending_=true;
    const auto own=ownPath_,notify=notifyPath_;const auto previous=credentialFiles_;
    credentialsWorker_.start([this,own,notify,previous]{
        QFile a(own),b(notify);a.open(QIODevice::ReadOnly);b.open(QIODevice::ReadOnly);
        QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream<<a.readAll()<<b.readAll();
        const bool changed=bytes!=previous;
        const auto next=changed?loadCredentials(own,notify):DonationCredentials{};
        QMetaObject::invokeMethod(this,[this,bytes,changed,next]{
            credentialsPending_=false;if(closing_)return;
            if(changed){credentialFiles_=bytes;applyCredentials(next);}
            if(credentialsAgain_){credentialsAgain_=false;refreshCredentials();}
        },Qt::QueuedConnection);
    });
}
void DonationsDock::applyCredentials(const DonationCredentials& next) {
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
void DonationsDock::render() {ozkperf::Scope perf(Q_FUNC_INFO);
    donations_telemetry::CpuScope cpu;
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
    auto serviceTotal=[&](const QString& service){
        const auto totals=feed_.totals(service);
        if(!totals.complete)return QString::fromUtf8("%1: считаем историю…").arg(service);
        const auto all=convertToRub(totals.all,rubPerUnit_);
        const auto month=convertToRub(totals.month,rubPerUnit_);
        if(!all.complete||!month.complete)return QString::fromUtf8("%1: нет курса одной из валют").arg(service);
        return QString::fromUtf8("%1: %2 всего · %3/мес.")
            .arg(service,compactRub(all.amount,all.converted),compactRub(month.amount,month.converted));
    };
    daTotals_->setVisible(credentials_.daEnabled);dpTotals_->setVisible(credentials_.dpEnabled);
    daTotals_->setText(serviceTotal("DonationAlerts"));dpTotals_->setText(serviceTotal("DonatePay"));
    const auto detail=[&](const QString& service){
        const auto totals=feed_.totals(service);
        if(!totals.complete)return QString::fromUtf8("Подсчёт истории продолжается. Результат сохраняется после каждой страницы.");
        return QString::fromUtf8("Исходные суммы\nВсего: %1\nЗа месяц: %2\nПересчёт по одному курсу от %3 — приблизительная оценка.")
            .arg(sums(totals.all),sums(totals.month),ratesDate_);
    };
    daTotals_->setToolTip(detail("DonationAlerts"));dpTotals_->setToolTip(detail("DonatePay"));
    const qint64 manualKopecks=manualServiceTotal(manualServices_);
    otherTotals_->setVisible(!manualServices_.isEmpty());
    otherTotals_->setText(QString::fromUtf8("Донаты с других сервисов: %1 ₽").arg(QLocale(QLocale::Russian,QLocale::Russia).toString(double(manualKopecks)/100.0,'f',manualKopecks%100?2:0)));
    QMap<QString,double> combined;
    bool totalsReady=true,anyService=!manualServices_.isEmpty();
    combined["RUB"]=double(manualKopecks)/100.0;
    for(const auto& service:{QString("DonationAlerts"),QString("DonatePay")}){
        if(service=="DonationAlerts"?!credentials_.daEnabled:!credentials_.dpEnabled)continue;
        const auto totals=feed_.totals(service);
        if((service=="DonationAlerts"?credentials_.da.isEmpty():credentials_.dp.isEmpty())&&!totals.complete)continue;
        anyService=true;
        if(!totals.complete){totalsReady=false;continue;}
        for(auto it=totals.all.cbegin();it!=totals.all.cend();++it)combined[it.key()]+=it.value();
    }
    const auto combinedRub=convertToRub(combined,rubPerUnit_);
    const double usdRate=rubPerUnit_.value("USD",0);
    combinedUsd_->setText(anyService&&totalsReady&&combinedRub.complete&&usdRate>0?
        QString::fromUtf8("≈ $%1").arg(QLocale(QLocale::Russian,QLocale::Russia).toString(combinedRub.amount/usdRate,'f',0)):
        QString::fromUtf8("—"));
    const auto rateDate=QDateTime::fromString(ratesDate_,Qt::ISODate).date();
    rateStatus_->setText(rateDate.isValid()?QString::fromUtf8("Сохранённый курс от %1 · без автоматического обновления").arg(rateDate.toString("dd.MM.yyyy")):
        ratesReply_?QString::fromUtf8("Загружаем курс…"):QString::fromUtf8("Курс не загружен"));
    combinedUsd_->setToolTip(QString::fromUtf8("Примерная сумма всех подключённых сервисов в USD по одному сохранённому курсу от %1. Это не фактическая сумма выплат.").arg(rateDate.toString("dd.MM.yyyy")));
    QByteArray signature;QDataStream signatureStream(&signature,QIODevice::WriteOnly);
    signatureStream<<feed_.loading()<<feed_.hasAccounts()<<feed_.daHistoryPermissionNeeded()<<credentials_.daEnabled;
    QHash<QString,QByteArray> versions;
    for(const auto& e:entries){
        QByteArray version;QDataStream data(&version,QIODevice::WriteOnly);
        data<<e.service<<e.id<<e.donor<<e.message<<e.currency<<e.amount<<e.at<<e.session;
        versions.insert(e.service+":"+e.id,version);signatureStream<<version;
    }
    if(signature==listSignature_)return;
    listSignature_=signature;
    while(feedLayout_->count()>0){auto* item=feedLayout_->takeAt(0);
        if(item->widget()&&!rows_.values().contains(item->widget()))delete item->widget();delete item;}
    auto previousRows=rows_;rows_.clear();
    QElapsedTimer rowBudget;rowBudget.start();int createdRows=0;bool unfinishedRows=false;
    if(feed_.daHistoryPermissionNeeded()&&credentials_.daEnabled)
        feedLayout_->addWidget(label(QString::fromUtf8("История DonationAlerts пока не разрешена. Откройте ⚙ и разрешите историю."),"muted"));
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
            const auto key=entry.service+":"+entry.id;
            auto* existing=previousRows.take(key);
            if(existing&&existing->property("donationVersion").toByteArray()==versions.value(key)){
                rows_.insert(key,existing);feedLayout_->addWidget(existing);continue;
            }
            if(createdRows>=8||rowBudget.elapsed()>=4){
                if(existing){rows_.insert(key,existing);feedLayout_->addWidget(existing);}
                unfinishedRows=true;continue;
            }
            delete existing;++createdRows;
            QVBoxLayout* inside=nullptr;auto* row=card(&inside);
            row->setProperty("donationVersion",versions.value(key));rows_.insert(key,row);
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
    qDeleteAll(previousRows);
    feedLayout_->addStretch();
    if(unfinishedRows&&!renderPending_){renderPending_=true;
        QTimer::singleShot(0,this,[this]{renderPending_=false;listSignature_.clear();render();});
    }
}

void DonationsDock::rebuildManualServices(){
    while(auto* item=manualList_->takeAt(0)){delete item->widget();delete item;}
    if(manualServices_.isEmpty()){manualList_->addWidget(label(QString::fromUtf8("Сервисы пока не добавлены. Нажмите +."),"muted"));return;}
    for(int i=0;i<manualServices_.size();++i){const auto& service=manualServices_[i];
        auto* row=new QWidget;auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
        layout->addWidget(label(service.name+" · "+QLocale(QLocale::Russian,QLocale::Russia).toString(double(service.kopecks)/100,'f',service.kopecks%100?2:0)+QString::fromUtf8(" ₽")),1);
        auto* edit=new QPushButton(QString::fromUtf8("Изменить"));layout->addWidget(edit);
        auto* remove=new QPushButton(QString::fromUtf8("×"));remove->setFixedWidth(28);remove->setToolTip(QString::fromUtf8("Удалить сервис"));layout->addWidget(remove);
        connect(edit,&QPushButton::clicked,this,[this,i]{editManualService(i);});
        connect(remove,&QPushButton::clicked,this,[this,i]{
            if(QMessageBox::question(this,QString::fromUtf8("Удалить сервис?"),QString::fromUtf8("Удалить ручную запись и исключить её сумму из общего итога?"))!=QMessageBox::Yes)return;
            auto next=manualServices_;next.removeAt(i);persistManualServices(next);
        });manualList_->addWidget(row);
    }
}
void DonationsDock::editManualService(int index){
    if(index<0&&manualServices_.size()>=100){settingsMessage_->setText(QString::fromUtf8("Можно добавить до 100 сервисов."));return;}
    QDialog dialog(this);dialog.setWindowTitle(QString::fromUtf8(index<0?"Добавить сервис":"Изменить сервис"));dialog.setStyleSheet(QString::fromUtf8(dockStyle));dialog.setMinimumWidth(300);dialog.setStyleSheet(dialog.styleSheet()+"QDialog{background:#080f19;}QDialog QLabel{color:#dfe8ff;font-size:12px;}QDialog QLineEdit,QDialog QDoubleSpinBox{background:#0c1420;color:#e7eeff;border:1px solid #344865;border-radius:6px;padding:5px;min-height:22px;}");
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    auto* name=new QLineEdit;name->setMaxLength(80);name->setObjectName("manualServiceName");name->setPlaceholderText(QString::fromUtf8("Название донатного сервиса"));
    auto* amount=new QDoubleSpinBox;amount->setObjectName("manualServiceAmount");amount->setLocale(QLocale(QLocale::Russian,QLocale::Russia));amount->setDecimals(2);amount->setRange(0,1e12);amount->setGroupSeparatorShown(true);amount->setSuffix(QString::fromUtf8(" ₽"));
    if(index>=0){name->setText(manualServices_[index].name);amount->setValue(double(manualServices_[index].kopecks)/100);}
    form->addRow(QString::fromUtf8("Название сервиса"),name);form->addRow(QString::fromUtf8("Получено за всё время, ₽"),amount);layout->addLayout(form);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Сохранить"));buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Save)->setEnabled(!name->text().trimmed().isEmpty());
    connect(name,&QLineEdit::textChanged,&dialog,[buttons](const QString& text){buttons->button(QDialogButtonBox::Save)->setEnabled(!text.trimmed().isEmpty());});
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    auto next=manualServices_;ManualService value{index<0?QUuid::createUuid().toString(QUuid::WithoutBraces):next[index].id,name->text().trimmed(),qint64(std::llround(amount->value()*100))};
    if(index<0)next.append(value);else next[index]=value;persistManualServices(next);
}
void DonationsDock::persistManualServices(const QVector<ManualService>& next){
    manualBox_->setEnabled(false);const auto path=manualPath_;
    credentialsWorker_.start([this,path,next]{const bool ok=saveManualServices(path,next);
        QMetaObject::invokeMethod(this,[this,next,ok]{if(closing_)return;manualBox_->setEnabled(true);
            if(!ok){settingsMessage_->setText(QString::fromUtf8("Не удалось сохранить ручные суммы. Предыдущие данные сохранены."));return;}
            manualServices_=next;rebuildManualServices();render();settingsMessage_->setText(QString::fromUtf8("Ручные суммы сохранены."));
        },Qt::QueuedConnection);
    });
}

DonationsDock::~DonationsDock() { shutdown(); }
void DonationsDock::shutdown() {
    if(closing_)return;
    closing_=true;credentialsTimer_->stop();credentialsWorker_.waitForDone();login_.stop();
    if(loginReply_)loginReply_->abort();if(ratesReply_)ratesReply_->abort();player_->stop();feed_.stop();
}
