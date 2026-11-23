// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <SDL.h>
#include <cstdint>

namespace JAHDeskToolbar {
enum class Action {
    Disconnect = 1,
    ToggleMouseMode,
    ToggleLocalCursor,
    SetResolution,
    OpenResolution,
};

// Native controls send actions to the existing SDL loop, which owns the session.
inline bool push(SDL_Window* window, Uint32 type, Action action, int width = 0, int height = 0)
{
    SDL_Event event {};
    event.type = type;
    event.user.windowID = SDL_GetWindowID(window);
    event.user.code = static_cast<Sint32>(action);
    event.user.data1 = reinterpret_cast<void*>(static_cast<uintptr_t>(width));
    event.user.data2 = reinterpret_cast<void*>(static_cast<uintptr_t>(height));
    return SDL_PushEvent(&event) == 1;
}

// The dimension bounds match Moonlight's custom resolution dialog.
inline bool validResolution(int width, int height)
{
    return width >= 256 && width <= 8192 && width % 2 == 0 &&
           height >= 256 && height <= 8192 && height % 2 == 0;
}

bool attach(SDL_Window* window, Uint32 eventType, bool absolute, bool cursorVisible, int width, int height);
void update(SDL_Window* window, bool absolute, bool cursorVisible, int width, int height);
void detach(SDL_Window* window);
bool promptResolution(SDL_Window* window, int& width, int& height);
}
