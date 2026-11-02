#pragma once
#include <QString>
#include <QVector>

struct YouTubeLink {
    int start=0;
    int length=0;
    QString id;
};

// Accept only ordinary YouTube video URLs. The ID is safe to place in an
// embed URL; arbitrary donation text never becomes a browser destination.
QVector<YouTubeLink> findYouTubeLinks(const QString& message);
QString donationTextWithYouTubeLinks(const QString& message);
