#pragma once

#include <QQueue>
#include <QSet>
#include <QString>

class UriLaunchQueue
{
public:
    bool enqueue(const QString& uri);
    void setReady();

    bool hasReadyRequest() const;
    QString takeNext();
    int pendingCount() const;

private:
    static constexpr int MaxRememberedUris = 128;

    QQueue<QString> m_PendingUris;
    QQueue<QString> m_SeenOrder;
    QSet<QString> m_SeenUris;
    bool m_Ready = false;
};
