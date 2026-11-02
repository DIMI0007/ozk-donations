#pragma once
#include "feed.hpp"
#include <QString>
#include <QVector>

QVector<DonationEntry> loadDonationCache(const QString& path,const QString& daToken,const QString& dpToken);
bool saveDonationCache(const QString& path,const QString& daToken,const QString& dpToken,
                       const QVector<DonationEntry>& entries);
