#pragma once
#include "feed.hpp"
#include <QString>
#include <QVector>
#include <QJsonObject>

QVector<DonationEntry> loadDonationCache(const QString& path,const QString& daToken,const QString& dpToken);
bool saveDonationCache(const QString& path,const QString& daToken,const QString& dpToken,
                       const QVector<DonationEntry>& entries);
QJsonObject loadProtectedDonationJson(const QString& path);
bool saveProtectedDonationJson(const QString& path,const QJsonObject& value);
QString donationTokenDigest(const QString& token);
