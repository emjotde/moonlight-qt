#include "streamdock.h"
#include "backend/streamdisplays.h"

#include <SDL_syswm.h>
#include <QtDebug>

namespace {
const wchar_t* DockClass = L"MoonlightStreamDock";
const int ButtonFirst = 100;
const int MonitorFirst = 1000;
const int KeyboardFirst = 2000;

void setText(HWND window, const QString& text)
{
    wchar_t oldText[128] = {};
    GetWindowTextW(window, oldText, sizeof(oldText) / sizeof(oldText[0]));
    if (QString::fromWCharArray(oldText) != text) {
        SetWindowTextW(window, reinterpret_cast<const wchar_t*>(text.utf16()));
        InvalidateRect(window, nullptr, FALSE);
    }
}
}

StreamDock::StreamDock(SDL_Window* streamWindow, StreamingPreferences::CaptureSysKeysMode mode)
    : m_StreamWindow(streamWindow), m_KeyboardMode(mode)
{
}

StreamDock::~StreamDock()
{
    m_Destroying = true;
    if (m_Window) {
        KillTimer(m_Window, 1);
        DestroyWindow(m_Window);
    }
    if (m_Font) {
        DeleteObject(m_Font);
    }
    Uint32 windowId = SDL_GetWindowID(m_StreamWindow);
    SDL_FilterEvents([](void* context, SDL_Event* event) -> int {
        return !(event->type == SDL_USEREVENT && event->user.code == EventCode &&
                 event->user.windowID == *static_cast<Uint32*>(context));
    }, &windowId);
}

bool StreamDock::initialize()
{
    SDL_SysWMinfo info = {};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(m_StreamWindow, &info) || info.subsystem != SDL_SYSWM_WINDOWS) {
        qWarning() << "Cannot attach stream controls to the streaming window:" << SDL_GetError();
        return false;
    }
    m_Owner = info.info.win.window;

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = DockClass;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        qWarning() << "Cannot register stream controls window:" << GetLastError();
        return false;
    }

    m_Window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, DockClass, L"Moonlight stream controls",
                               WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, m_Owner, nullptr,
                               windowClass.hInstance, this);
    if (!m_Window) {
        qWarning() << "Cannot create stream controls:" << GetLastError();
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        m_Buttons[i] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW,
                                       0, 0, 1, 1, m_Window,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(ButtonFirst + i)),
                                       windowClass.hInstance, nullptr);
        if (!m_Buttons[i]) {
            qWarning() << "Cannot create stream control button:" << GetLastError();
            return false;
        }
    }
    setText(m_Buttons[1], QObject::tr("Monitor"));
    setText(m_Buttons[3], QObject::tr("Disconnect"));
    setKeyboardMode(m_KeyboardMode);
    if (!SetTimer(m_Window, 1, 100, nullptr)) {
        qWarning() << "Cannot start stream controls timer:" << GetLastError();
        return false;
    }
    updatePosition();
    return true;
}

int StreamDock::scaled(int value) const
{
    return MulDiv(value, m_Dpi, 96);
}

bool StreamDock::isAttached() const
{
    SDL_SysWMinfo info = {};
    SDL_VERSION(&info.version);
    return m_Window && IsWindow(m_Window) && SDL_GetWindowWMInfo(m_StreamWindow, &info) &&
           info.info.win.window == m_Owner;
}

bool StreamDock::request(Action action, int value)
{
    SDL_Event event = {};
    event.type = SDL_USEREVENT;
    event.user.windowID = SDL_GetWindowID(m_StreamWindow);
    event.user.code = EventCode;
    event.user.data1 = reinterpret_cast<void*>(static_cast<intptr_t>(action));
    event.user.data2 = reinterpret_cast<void*>(static_cast<intptr_t>(value));
    if (SDL_PushEvent(&event) != 1) {
        qWarning() << "Cannot queue stream control action:" << SDL_GetError();
        const QString text = QObject::tr("Unable to apply the stream control action. Try again.");
        MessageBoxW(m_Owner, reinterpret_cast<const wchar_t*>(text.utf16()), L"Moonlight", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void StreamDock::toggle()
{
    setExpanded(!m_Expanded);
    if (m_Expanded) {
        m_HideDelay = 5000;
    }
    updatePosition();
}

void StreamDock::collapse()
{
    setExpanded(false);
}

void StreamDock::setExpanded(bool expanded)
{
    if (m_Expanded == expanded || m_Destroying) {
        return;
    }
    if (!expanded && GetForegroundWindow() == m_Owner) {
        SetFocus(m_Owner);
    }
    if (!request(LocalControls, expanded ? 1 : 0)) {
        return;
    }
    m_Expanded = expanded;
    m_LastHover = SDL_GetTicks();
    m_HideDelay = 1500;
    for (HWND button : m_Buttons) {
        ShowWindow(button, expanded ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    if (!expanded) {
        ShowWindow(m_Window, SW_HIDE);
        RedrawWindow(m_Owner, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    InvalidateRect(m_Window, nullptr, FALSE);
}

void StreamDock::setKeyboardMode(StreamingPreferences::CaptureSysKeysMode mode)
{
    m_KeyboardMode = mode;
    const QString name = mode == StreamingPreferences::CSK_OFF ? QObject::tr("Local") :
                         mode == StreamingPreferences::CSK_ALWAYS ? QObject::tr("Remote") : QObject::tr("Auto");
    setText(m_Buttons[2], QObject::tr("Keys: %1").arg(name));
}

void StreamDock::updatePosition()
{
    const HWND foreground = GetForegroundWindow();
    if (IsIconic(m_Owner) || !IsWindowVisible(m_Owner) ||
            (!m_MenuOpen && foreground != m_Owner && foreground != m_Window && !IsChild(m_Window, foreground))) {
        setExpanded(false);
        m_RevealStart = 0;
        ShowWindow(m_Window, SW_HIDE);
        return;
    }

    using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
    static auto getDpi = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const UINT dpi = getDpi ? getDpi(m_Owner) : 96;
    if (!m_Font || m_Dpi != dpi) {
        m_Dpi = dpi ? dpi : 96;
        if (m_Font) {
            DeleteObject(m_Font);
        }
        m_Font = CreateFontW(-MulDiv(10, m_Dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!m_Font) {
            qWarning() << "Cannot create stream controls font:" << GetLastError();
        }
    }

    RECT client = {};
    POINT origin = {};
    if (!GetClientRect(m_Owner, &client) || !ClientToScreen(m_Owner, &origin)) {
        qWarning() << "Cannot position stream controls:" << GetLastError();
        ShowWindow(m_Window, SW_HIDE);
        return;
    }
    const int availableWidth = qMax(1, static_cast<int>(client.right) - scaled(12));
    const int panelWidth = qMin(scaled(560), availableWidth);
    const int columns = panelWidth < scaled(480) ? 2 : 4;
    POINT cursor = {};
    GetCursorPos(&cursor);
    if (!m_Expanded) {
        ShowWindow(m_Window, SW_HIDE);
        const int center = origin.x + client.right / 2;
        const int halfWidth = qMin(scaled(80), availableWidth / 2);
        const RECT revealRegion = {center - halfWidth, origin.y,
                                    center + halfWidth, origin.y + scaled(8)};
        if (PtInRect(&revealRegion, cursor) && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            if (!m_RevealStart) {
                m_RevealStart = SDL_GetTicks();
            }
            else if (SDL_GetTicks() - m_RevealStart >= 250) {
                setExpanded(true);
                m_RevealStart = 0;
            }
        }
        else {
            m_RevealStart = 0;
        }
        return;
    }
    m_RevealStart = 0;
    const int width = panelWidth;
    const int height = scaled(columns == 4 ? 42 : 80);
    const int left = origin.x + (client.right - width) / 2;
    const int top = origin.y + scaled(2);

    SetWindowPos(m_Window, HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (m_Expanded) {
        const int gap = scaled(4);
        const int buttonWidth = (width - (columns + 1) * gap) / columns;
        for (int i = 0; i < 4; ++i) {
            SetWindowPos(m_Buttons[i], nullptr, gap + (i % columns) * (buttonWidth + gap),
                         gap + (i / columns) * scaled(38), buttonWidth, scaled(34),
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
    }
    setText(m_Buttons[0], (SDL_GetWindowFlags(m_StreamWindow) & SDL_WINDOW_FULLSCREEN) ?
                                 QObject::tr("Windowed") : QObject::tr("Full screen"));

    const RECT rectangle = {left, top, left + width, top + height};
    if (PtInRect(&rectangle, cursor) || m_MenuOpen) {
        m_LastHover = SDL_GetTicks();
        m_HideDelay = 1500;
    }
    else if (m_Expanded && SDL_GetTicks() - m_LastHover > m_HideDelay) {
        setExpanded(false);
    }
}

void StreamDock::showMenu(bool monitors)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        qWarning() << "Cannot create stream controls menu:" << GetLastError();
        return;
    }
    if (monitors) {
        const auto displays = StreamDisplays::available();
        for (int i = 0; i < SDL_GetNumVideoDisplays(); ++i) {
            const char* name = SDL_GetDisplayName(i);
            QString text = QObject::tr("Monitor %1").arg(i + 1);
            for (const auto& display : displays) {
                if (name && display.name == QString::fromUtf8(name)) {
                    text = display.description;
                    break;
                }
            }
            AppendMenuW(menu, MF_STRING | (SDL_GetWindowDisplayIndex(m_StreamWindow) == i ? MF_CHECKED : 0),
                         MonitorFirst + i, reinterpret_cast<const wchar_t*>(text.utf16()));
        }
    }
    else {
        const int modes[] = {StreamingPreferences::CSK_FULLSCREEN, StreamingPreferences::CSK_OFF,
                             StreamingPreferences::CSK_ALWAYS};
        const QString labels[] = {QObject::tr("Auto - local when windowed, remote in fullscreen"),
                                  QObject::tr("Local - system shortcuts stay on this PC"),
                                  QObject::tr("Remote - system shortcuts go to the host")};
        for (int i = 0; i < 3; ++i) {
            AppendMenuW(menu, MF_STRING | (m_KeyboardMode == modes[i] ? MF_CHECKED : 0),
                         KeyboardFirst + modes[i], reinterpret_cast<const wchar_t*>(labels[i].utf16()));
        }
    }

    RECT button = {};
    GetWindowRect(m_Buttons[monitors ? 1 : 2], &button);
    m_MenuOpen = true;
    SetForegroundWindow(m_Owner);
    const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN,
                                         button.left, button.bottom, 0, m_Owner, nullptr);
    PostMessageW(m_Owner, WM_NULL, 0, 0);
    m_MenuOpen = false;
    m_LastHover = SDL_GetTicks();
    DestroyMenu(menu);
    if (selected) {
        request(monitors ? MoveToMonitor : KeyboardMode,
                static_cast<int>(selected) - (monitors ? MonitorFirst : KeyboardFirst));
    }
}

void StreamDock::drawButton(const DRAWITEMSTRUCT& item)
{
    const COLORREF background = item.itemState & ODS_SELECTED ? RGB(85, 65, 115) : RGB(55, 55, 62);
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, RGB(245, 245, 245));
    HGDIOBJ previousFont = SelectObject(item.hDC, m_Font ? m_Font : GetStockObject(DEFAULT_GUI_FONT));
    wchar_t text[128] = {};
    GetWindowTextW(item.hwndItem, text, sizeof(text) / sizeof(text[0]));
    RECT textRect = item.rcItem;
    DrawTextW(item.hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(item.hDC, previousFont);
    if (item.itemState & ODS_FOCUS) {
        DrawFocusRect(item.hDC, &item.rcItem);
    }
}

LRESULT CALLBACK StreamDock::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto dock = reinterpret_cast<StreamDock*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        dock = static_cast<StreamDock*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        dock->m_Window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dock));
    }
    return dock ? dock->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT StreamDock::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_NCDESTROY: {
        const HWND window = m_Window;
        m_Window = nullptr;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        updatePosition();
        return 0;
    case WM_LBUTTONUP:
        setExpanded(true);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(m_Window, &paint);
        RECT rectangle;
        GetClientRect(m_Window, &rectangle);
        HBRUSH brush = CreateSolidBrush(RGB(28, 28, 32));
        FillRect(dc, &rectangle, brush);
        DeleteObject(brush);
        EndPaint(m_Window, &paint);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam) - ButtonFirst) {
            case 0: request(ToggleFullscreen); break;
            case 1: showMenu(true); break;
            case 2: showMenu(false); break;
            case 3: request(Disconnect); break;
            }
        }
        return 0;
    }
    return DefWindowProcW(m_Window, message, wParam, lParam);
}
