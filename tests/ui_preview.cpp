#include "dock.hpp"
#include "youtube_player.hpp"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>

// The player is never opened in a UI preview; keep OBS and browser data out.
YouTubePlayer::YouTubePlayer(QWidget* parent):QWidget(parent) {}
bool YouTubePlayer::play(const QString&) {return false;}
void YouTubePlayer::stop() {}
void YouTubePlayer::resizeEvent(QResizeEvent* event) {QWidget::resizeEvent(event);}

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setFont(QFont("Segoe UI",9));
    const QString output=argc>1?QString::fromLocal8Bit(argv[1]):"ui-previews";
    if(!QDir().mkpath(output))return 1;
    DonationsDock dock;
    dock.resize(360,740);dock.show();app.processEvents();
    dock.previewPopulate();app.processEvents();
    auto* firstRow=dock.findChild<QWidget*>("newDonationCard");
    if(!firstRow)return 9;
    auto* feed=dock.findChild<DonationFeed*>();if(!feed)return 11;
    for(int i=0;i<100;++i)QMetaObject::invokeMethod(feed,"updated",Qt::DirectConnection);
    app.processEvents();
    if(dock.findChild<QWidget*>("newDonationCard")!=firstRow)return 10;
    if(!dock.grab().save(output+"/home-360.png"))return 2;
    dock.resize(300,700);app.processEvents();
    if(!dock.grab().save(output+"/home-300.png"))return 3;
    dock.resize(360,740);app.processEvents();
    auto* gear=dock.findChild<QPushButton*>("SettingsShortcut");
    if(!gear||gear->size()!=QSize(30,28)||gear->iconSize()!=QSize(19,19))return 4;
    gear->click();app.processEvents();
    auto* settings=dock.findChild<QWidget*>("settingsPage");
    if(!settings||!settings->isVisible())return 5;
    if(!dock.grab().save(output+"/settings-360.png"))return 6;
    auto* scroll=settings->findChild<QScrollArea*>();
    if(!scroll)return 7;
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());app.processEvents();
    if(!dock.grab().save(output+"/settings-bottom-360.png"))return 8;
    dock.shutdown();
    return 0;
}
