#include "dock.hpp"
#include "telemetry.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <util/bmem.h>
#include <QCoreApplication>
#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QSslSocket>
#include <QTimer>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("DMITRII")
extern "C" __declspec(dllexport) bool ozk_donations_telemetry_v1(std::uint64_t* tx,std::uint64_t* rx){
    if(!tx||!rx)return false;
    *tx=donations_telemetry::sent.load();*rx=donations_telemetry::received.load();return true;
}
extern "C" __declspec(dllexport) bool ozk_donations_telemetry_v2(std::uint64_t* tx,std::uint64_t* rx,
    std::uint64_t* cpu,std::uint64_t* data){
    if(!tx||!rx||!cpu||!data)return false;
    *tx=donations_telemetry::sent.load();*rx=donations_telemetry::received.load();
    *cpu=donations_telemetry::cpuNs.load();*data=donations_telemetry::dataBytes.load();return true;
}
namespace {
QPointer<DonationsDock> dockPanel;
bool exiting=false;
QString qtPluginPath;
QString visibilityPath(){
    char* raw=obs_module_config_path("dock-visibility.ini");
    if(!raw)return {};
    const QString path=QString::fromUtf8(raw);bfree(raw);return path;
}
void saveVisibility(){
    auto* window=static_cast<QMainWindow*>(obs_frontend_get_main_window());
    auto* dock=window?window->findChild<QDockWidget*>("ozk-donations-dock"):nullptr;
    if(dock){QSettings settings(visibilityPath(),QSettings::IniFormat);
        settings.setValue("visible",dock->toggleViewAction()->isChecked());settings.sync();}
}
void installDock(){
    if(exiting||dockPanel)return;
    auto* window=static_cast<QMainWindow*>(obs_frontend_get_main_window());
    if(!window)return;
    auto* panel=new DonationsDock;
    if(!obs_frontend_add_dock_by_id("ozk-donations-dock","OZK Donations",panel)){
        delete panel;blog(LOG_ERROR,"[OZK Donations] Dock registration failed");return;
    }
    dockPanel=panel;
    auto* dock=window->findChild<QDockWidget*>("ozk-donations-dock");
    if(!dock)return;
    const QString path=visibilityPath();
    QTimer::singleShot(0,dock,[dock,window,path]{
        QSettings saved(path,QSettings::IniFormat);
        if(!saved.contains("visible")){dock->setFloating(false);window->addDockWidget(Qt::RightDockWidgetArea,dock);}
        dock->setVisible(saved.value("visible",true).toBool());
        QObject::connect(dock->toggleViewAction(),&QAction::toggled,dock,[path,window](bool visible){
            if(!window->isVisible()||window->isMinimized())return;
            QSettings state(path,QSettings::IniFormat);state.setValue("visible",visible);state.sync();
        });
    });
}
void frontend(obs_frontend_event event,void*){
    if(exiting)return;
    if(event==OBS_FRONTEND_EVENT_FINISHED_LOADING)installDock();
    if(event==OBS_FRONTEND_EVENT_EXIT){exiting=true;saveVisibility();if(dockPanel)dockPanel->shutdown();}
}
}
MODULE_EXPORT const char* obs_module_name(void){return "OZK Donations";}
MODULE_EXPORT const char* obs_module_description(void){return "DonationAlerts and DonatePay donations in a separate OBS dock";}
bool obs_module_load(void){
    exiting=false;
    char* raw=obs_module_file("qt-plugins");
    if(raw){qtPluginPath=QString::fromUtf8(raw);bfree(raw);QCoreApplication::addLibraryPath(qtPluginPath);}
    if(!QSslSocket::supportsSsl()&&QSslSocket::availableBackends().contains("schannel"))
        QSslSocket::setActiveBackend("schannel");
    obs_frontend_add_event_callback(frontend,nullptr);
    blog(LOG_INFO,"[OZK Donations] 0.6.1 loaded");return true;
}
void obs_module_unload(void){
    exiting=true;obs_frontend_remove_event_callback(frontend,nullptr);
    if(dockPanel){dockPanel->shutdown();obs_frontend_remove_dock("ozk-donations-dock");dockPanel=nullptr;}
    if(!qtPluginPath.isEmpty()){QCoreApplication::removeLibraryPath(qtPluginPath);qtPluginPath.clear();}
}
