#include "urilaunchqueue.h"

bool UriLaunchQueue::enqueue(const QString& uri)
{
    if (m_SeenUris.contains(uri)) {
        return false;
    }
    m_SeenUris.insert(uri);
    m_SeenOrder.enqueue(uri);
    if (m_SeenOrder.size() > MaxRememberedUris) {
        m_SeenUris.remove(m_SeenOrder.dequeue());
    }
    m_PendingUris.enqueue(uri);
    return true;
}

void UriLaunchQueue::setReady()
{
    m_Ready = true;
}

bool UriLaunchQueue::hasReadyRequest() const
{
    return m_Ready && !m_PendingUris.isEmpty();
}

QString UriLaunchQueue::takeNext()
{
    return hasReadyRequest() ? m_PendingUris.dequeue() : QString();
}

int UriLaunchQueue::pendingCount() const
{
    return m_PendingUris.size();
}
