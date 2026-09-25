QT += core testlib quick quickcontrols2
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = tst_appstreamingsettings
TEMPLATE = app

INCLUDEPATH += ../app
DEFINES += TEST_GUI_DIR=\\\"$$PWD/../app/gui\\\"

SOURCES += \
    tst_appstreamingsettings.cpp \
    ../app/settings/appstreamingsettings.cpp \
    ../app/settings/externallaunchtrust.cpp \
    ../app/settings/streamingpreferences.cpp \
    ../app/cli/commandlineparser.cpp \
    ../app/streaming/streamlaunchrequest.cpp \
    ../app/streaming/urilaunchrequest.cpp \
    ../app/backend/streamdisplays.cpp \
    ../app/wm.cpp

HEADERS += \
    ../app/settings/appstreamingsettings.h \
    ../app/settings/externallaunchtrust.h \
    ../app/settings/streamingpreferences.h \
    ../app/cli/commandlineparser.h
HEADERS += ../app/backend/streamdisplays.h \
    ../app/streaming/input/keyboardrouting.h \
    ../app/streaming/streamlaunchrequest.h
HEADERS += ../app/streaming/urilaunchrequest.h

win32 {
    INCLUDEPATH += ../libs/windows/include
    contains(QT_ARCH, x86_64): INCLUDEPATH += ../libs/windows/include/x64
    contains(QT_ARCH, arm64): INCLUDEPATH += ../libs/windows/include/arm64
    contains(QT_ARCH, i386): INCLUDEPATH += ../libs/windows/include/x86
    LIBS += user32.lib
    DEFINES += _USE_MATH_DEFINES
}
unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += sdl2
}
macx {
    INCLUDEPATH += ../libs/mac/Frameworks/SDL2.framework/Versions/A/Headers
}
