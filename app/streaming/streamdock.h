#pragma once

#include "settings/streamingpreferences.h"

#include <SDL.h>
#include <qt_windows.h>

class StreamDock
{
public:
    enum { EventCode = 107 };
    enum Action { ToggleFullscreen, MoveToMonitor, KeyboardMode, Disconnect, LocalControls };

    explicit StreamDock(SDL_Window* streamWindow, StreamingPreferences::CaptureSysKeysMode mode);
    ~StreamDock();

    bool initialize();
    bool isAttached() const;
    void toggle();
    void setKeyboardMode(StreamingPreferences::CaptureSysKeysMode mode);
    bool isExpanded() const { return m_Expanded; }
    void collapse();

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool request(Action action, int value = 0);
    void setExpanded(bool expanded);
    void updatePosition();
    void showMenu(bool monitors);
    void drawButton(const DRAWITEMSTRUCT& item);
    int scaled(int value) const;

    SDL_Window* m_StreamWindow;
    HWND m_Owner = nullptr;
    HWND m_Window = nullptr;
    HWND m_Buttons[4] = {};
    HFONT m_Font = nullptr;
    StreamingPreferences::CaptureSysKeysMode m_KeyboardMode;
    bool m_Expanded = false;
    bool m_MenuOpen = false;
    bool m_Destroying = false;
    UINT m_Dpi = 96;
    Uint32 m_LastHover = 0;
    Uint32 m_HideDelay = 1500;
    Uint32 m_RevealStart = 0;
};
