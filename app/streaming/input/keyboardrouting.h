#pragma once

#include "settings/streamingpreferences.h"

#include <QSet>
#include <SDL_events.h>

class KeyboardRouting
{
public:
    static bool shouldCapture(StreamingPreferences::CaptureSysKeysMode mode,
                              bool fullscreen, bool inputActive, bool localControlsActive)
    {
        return inputActive && !localControlsActive &&
               (mode == StreamingPreferences::CSK_ALWAYS ||
                (mode == StreamingPreferences::CSK_FULLSCREEN && fullscreen));
    }

    bool consumeLocalShortcut(const SDL_KeyboardEvent& event, bool systemKeysCaptured)
    {
        const auto key = event.keysym.scancode;
        if (event.type == SDL_KEYUP && m_LocalKeys.remove(key)) {
            return true;
        }

        if (!systemKeysCaptured &&
                ((event.keysym.mod & KMOD_GUI) || key == SDL_SCANCODE_LGUI || key == SDL_SCANCODE_RGUI)) {
            if (event.type == SDL_KEYDOWN) {
                m_LocalKeys.insert(key);
            }
            return true;
        }
        return false;
    }

private:
    QSet<SDL_Scancode> m_LocalKeys;
};
