#pragma once

#include <QRect>
#include <QString>
#include <QVector>

struct StreamDisplay
{
    QString id;
    QString name;
    QString description;
    QRect geometry;
};

class StreamDisplays
{
public:
    static QVector<StreamDisplay> available();
    static int find(const QVector<StreamDisplay>& displays, const QString& id);
};
