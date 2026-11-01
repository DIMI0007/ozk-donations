#pragma once
#include <QString>

struct DonationCredentials {
    QString da;
    QString dp;
    bool daFromNotify = false;
    bool dpFromNotify = false;
    bool daEnabled = true;
    bool dpEnabled = true;
};

// Reads Notify's encrypted settings under the same Windows user. Never writes to Notify.
DonationCredentials loadCredentials(const QString& ownSettingsPath,
                                    const QString& notifySettingsPath);
bool saveOwnCredentials(const QString& path, const QString& da, const QString& dp);
bool saveServiceChoices(const QString& path,bool daEnabled,bool dpEnabled);
QString ownSettingsPath();
QString notifySettingsPath();
