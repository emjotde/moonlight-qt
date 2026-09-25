#include "streamdisplays.h"

#include <QGuiApplication>
#include <QScreen>
#include <QtDebug>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

QVector<StreamDisplay> StreamDisplays::available()
{
    QVector<StreamDisplay> result;
    for (QScreen* screen : QGuiApplication::screens()) {
        StreamDisplay display;
        display.name = screen->name();
        display.geometry = screen->geometry();
        QString label = screen->model().isEmpty() ? display.name : screen->model();

#ifdef Q_OS_WIN
        const QPoint center = display.geometry.center();
        const POINT point = {center.x(), center.y()};
        const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONULL);
        MONITORINFOEXW monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);
        DISPLAY_DEVICEW device = {};
        device.cb = sizeof(device);
        if (monitor && GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&monitorInfo)) &&
                EnumDisplayDevicesW(monitorInfo.szDevice, 0,
                                &device, EDD_GET_DEVICE_INTERFACE_NAME) && device.DeviceID[0]) {
            display.id = QString::fromWCharArray(device.DeviceID).toLower();
        }
        else {
            qWarning() << "Monitor device identity unavailable for" << display.name
                       << "- using screen metadata";
        }
#endif

        if (display.id.isEmpty()) {
            const QString identity = screen->serialNumber().isEmpty() ? display.name : screen->serialNumber();
            display.id = QStringLiteral("screen:%1:%2:%3")
                             .arg(screen->manufacturer(), screen->model(), identity);
        }

        display.description = QObject::tr("%1: %2 (%3 x %4, %5)")
            .arg(result.size() + 1).arg(label)
            .arg(qRound(display.geometry.width() * screen->devicePixelRatio()))
            .arg(qRound(display.geometry.height() * screen->devicePixelRatio()))
            .arg(display.geometry.height() > display.geometry.width() ?
                     QObject::tr("portrait") : QObject::tr("landscape"));
        result.append(display);
    }
    return result;
}

int StreamDisplays::find(const QVector<StreamDisplay>& displays, const QString& id)
{
    if (!id.isEmpty()) {
        for (int i = 0; i < displays.size(); ++i) {
            if (displays[i].id == id) {
                return i;
            }
        }
    }
    return -1;
}
