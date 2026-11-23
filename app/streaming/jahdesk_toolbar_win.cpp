// SPDX-License-Identifier: GPL-3.0-or-later

#include "jahdesk_toolbar.h"

#include <SDL_syswm.h>
#include <commctrl.h>

#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace JAHDeskToolbar {
namespace {

enum : UINT {
    AbsoluteMouse = 100,
    RelativeMouse,
    LocalCursor,
    CurrentResolution,
    CustomResolution,
    Disconnect,
    ResolutionBase = 200,
    WidthInput = 301,
    HeightInput,
    ValidationMessage,
};

struct Resolution {
    int width;
    int height;
};

constexpr Resolution Resolutions[] = {
    {1280, 720}, {1600, 900}, {1920, 1080}, {1920, 1200},
    {2560, 1440}, {3440, 1440}, {3840, 2160},
};
constexpr UINT_PTR ToolbarSubclass = 1;

struct Toolbar {
    SDL_Window* video;
    Uint32 eventType;
    HMENU menu;
    HMENU settings;
    HMENU resolutions;
    bool absolute;
    Resolution size;
};

/// Return the HWND that SDL owns and that the renderer already draws into.
HWND nativeWindow(SDL_Window* video)
{
    SDL_SysWMinfo info = {};
    SDL_VERSION(&info.version);
    return SDL_GetWindowWMInfo(video, &info) && info.subsystem == SDL_SYSWM_WINDOWS ?
        info.info.win.window : nullptr;
}

/// Format a resolution for display in the native menu.
std::wstring resolutionLabel(Resolution size)
{
    return std::to_wstring(size.width) + L" \u00d7 " + std::to_wstring(size.height);
}

/// Add a submenu whose lifetime is owned by its parent after it is attached.
HMENU appendSubmenu(HMENU menu, const wchar_t* title)
{
    HMENU submenu = CreatePopupMenu();
    if (!submenu) {
        return nullptr;
    }
    if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), title)) {
        DestroyMenu(submenu);
        return nullptr;
    }
    return submenu;
}

/// Append a DWORD to a Win32 dialog template, which is serialized in WORDs.
void appendDword(std::vector<WORD>& words, DWORD value)
{
    words.push_back(LOWORD(value));
    words.push_back(HIWORD(value));
}

/// Append a nul-terminated UTF-16 resource string to the dialog template.
void appendText(std::vector<WORD>& words, const wchar_t* text)
{
    while (*text) {
        words.push_back(*text++);
    }
    words.push_back(0);
}

/// Add a dword-aligned standard Win32 control to the resolution dialog.
void appendControl(std::vector<WORD>& words, DWORD style, WORD windowClass, WORD id,
                   WORD x, WORD y, WORD width, WORD height, const wchar_t* text = L"")
{
    if (words.size() % 2) {
        words.push_back(0);
    }
    appendDword(words, WS_CHILD | WS_VISIBLE | style);
    appendDword(words, 0);
    words.insert(words.end(), {x, y, width, height, id, 0xffff, windowClass});
    appendText(words, text);
    words.push_back(0);
}

/// Build a standard dialog with two editable dimensions and validation text.
std::vector<WORD> resolutionTemplate()
{
    constexpr WORD Button = 0x80;
    constexpr WORD Edit = 0x81;
    constexpr WORD Static = 0x82;
    constexpr DWORD EditStyle = WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL;
    std::vector<WORD> words;
    appendDword(words, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT);
    appendDword(words, 0);
    words.insert(words.end(), {7, 0, 0, 220, 93, 0, 0});
    appendText(words, L"\u81ea\u5b9a\u4e49\u5206\u8fa8\u7387");
    words.push_back(9);
    appendText(words, L"Segoe UI");

    appendControl(words, SS_LEFT, Static, 0xffff, 10, 12, 30, 13, L"\u5bbd\u5ea6");
    appendControl(words, EditStyle, Edit, WidthInput, 45, 10, 55, 14);
    appendControl(words, SS_LEFT, Static, 0xffff, 112, 12, 30, 13, L"\u9ad8\u5ea6");
    appendControl(words, EditStyle, Edit, HeightInput, 147, 10, 60, 14);
    appendControl(words, SS_LEFT, Static, ValidationMessage, 10, 34, 197, 22);
    appendControl(words, WS_TABSTOP | BS_DEFPUSHBUTTON, Button, IDOK,
                  88, 67, 62, 15, L"\u5e94\u7528\u5e76\u91cd\u8fde");
    appendControl(words, WS_TABSTOP | BS_PUSHBUTTON, Button, IDCANCEL,
                  157, 67, 50, 15, L"\u53d6\u6d88");
    return words;
}

/// Read a complete decimal dimension. Reject pasted text containing non-digits.
int readDimension(HWND dialog, int control)
{
    wchar_t text[5] = {};
    HWND input = GetDlgItem(dialog, control);
    const int length = GetWindowTextLengthW(input);
    if (length <= 0 || length >= static_cast<int>(std::size(text)) ||
        GetWindowTextW(input, text, static_cast<int>(std::size(text))) != length) {
        return 0;
    }
    int pixels = 0;
    for (int index = 0; index < length; ++index) {
        const wchar_t digit = text[index];
        if (digit < L'0' || digit > L'9') {
            return 0;
        }
        pixels = pixels * 10 + (digit - L'0');
    }
    return pixels;
}

/// Read the typed resolution and close the native sheet only on valid input.
INT_PTR CALLBACK resolutionDialog(HWND dialog, UINT message, WPARAM parameter, LPARAM data)
{
    if (message == WM_INITDIALOG) {
        auto* size = reinterpret_cast<Resolution*>(data);
        SetWindowLongPtrW(dialog, DWLP_USER, data);
        SetDlgItemInt(dialog, WidthInput, size->width, FALSE);
        SetDlgItemInt(dialog, HeightInput, size->height, FALSE);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(parameter) == IDCANCEL)) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message != WM_COMMAND || LOWORD(parameter) != IDOK) {
        return FALSE;
    }

    const int width = readDimension(dialog, WidthInput);
    const int height = readDimension(dialog, HeightInput);
    if (!validResolution(width, height)) {
        SetDlgItemTextW(dialog, ValidationMessage,
                        L"\u5bbd\u5ea6\u548c\u9ad8\u5ea6\u5fc5\u987b\u662f 256 \u81f3 8192 \u7684\u5076\u6570\u3002");
        SetFocus(GetDlgItem(dialog, validResolution(width, width) ? HeightInput : WidthInput));
        return TRUE;
    }
    auto* size = reinterpret_cast<Resolution*>(GetWindowLongPtrW(dialog, DWLP_USER));
    *size = {width, height};
    EndDialog(dialog, IDOK);
    return TRUE;
}

/// Show a native resolution sheet and return whether it supplied new dimensions.
bool editResolution(HWND owner, Resolution& size)
{
    const auto resource = resolutionTemplate();
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                                   reinterpret_cast<LPCDLGTEMPLATEW>(resource.data()),
                                   owner, resolutionDialog,
                                   reinterpret_cast<LPARAM>(&size)) == IDOK;
}

/// Queue an existing Moonlight event when a native menu command is selected.
bool dispatchMenu(Toolbar& toolbar, UINT command)
{
    switch (command) {
    case AbsoluteMouse:
    case RelativeMouse:
        if ((command == AbsoluteMouse) != toolbar.absolute) {
            push(toolbar.video, toolbar.eventType, Action::ToggleMouseMode);
        }
        return true;
    case LocalCursor:
        if (toolbar.absolute) {
            push(toolbar.video, toolbar.eventType, Action::ToggleLocalCursor);
        }
        return true;
    case Disconnect:
        push(toolbar.video, toolbar.eventType, Action::Disconnect);
        return true;
    case CustomResolution:
        push(toolbar.video, toolbar.eventType, Action::OpenResolution);
        return true;
    default:
        if (command < ResolutionBase || command >= ResolutionBase + std::size(Resolutions)) {
            return false;
        }
        const auto& size = Resolutions[command - ResolutionBase];
        if (size.width != toolbar.size.width || size.height != toolbar.size.height) {
            push(toolbar.video, toolbar.eventType, Action::SetResolution, size.width, size.height);
        }
        return true;
    }
}

/// Process only our menu IDs; SDL continues to own the native video and input events.
LRESULT CALLBACK toolbarWindowProc(HWND window, UINT message, WPARAM parameter, LPARAM data,
                                   UINT_PTR subclass, DWORD_PTR reference)
{
    auto* toolbar = reinterpret_cast<Toolbar*>(reference);
    if (message == WM_COMMAND && data == 0 && HIWORD(parameter) == 0 &&
        dispatchMenu(*toolbar, LOWORD(parameter))) {
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, toolbarWindowProc, subclass);
        // Windows destroys menus still associated with a destroyed window.
        delete toolbar;
    }
    return DefSubclassProc(window, message, parameter, data);
}

/// Return the current toolbar data associated with this SDL HWND.
Toolbar* getToolbar(HWND window)
{
    DWORD_PTR reference = 0;
    return window && GetWindowSubclass(window, toolbarWindowProc, ToolbarSubclass, &reference) ?
        reinterpret_cast<Toolbar*>(reference) : nullptr;
}

/// Populate the menus. Settings live in the title chrome outside the video client rect.
bool populateMenu(Toolbar& toolbar)
{
    toolbar.settings = appendSubmenu(toolbar.menu, L"\u4e32\u6d41\u8bbe\u7f6e");
    if (!toolbar.settings) {
        return false;
    }
    if (!AppendMenuW(toolbar.settings, MF_STRING, AbsoluteMouse, L"\u4e3b\u63a7\u7aef\u9f20\u6807\uff08\u7edd\u5bf9\u5750\u6807\uff09") ||
        !AppendMenuW(toolbar.settings, MF_STRING, RelativeMouse, L"\u6e38\u620f\u9f20\u6807\uff08\u76f8\u5bf9\u79fb\u52a8\uff09") ||
        !AppendMenuW(toolbar.settings, MF_STRING, LocalCursor, L"\u663e\u793a\u672c\u5730\u5149\u6807") ||
        !AppendMenuW(toolbar.settings, MF_SEPARATOR, 0, nullptr)) {
        return false;
    }
    toolbar.resolutions = appendSubmenu(toolbar.settings, L"\u5206\u8fa8\u7387\uff08\u5e94\u7528\u5e76\u91cd\u8fde\uff09");
    if (!toolbar.resolutions ||
        !AppendMenuW(toolbar.resolutions, MF_STRING | MF_GRAYED, CurrentResolution, L"")) {
        return false;
    }
    for (UINT index = 0; index < std::size(Resolutions); ++index) {
        if (!AppendMenuW(toolbar.resolutions, MF_STRING, ResolutionBase + index,
                         resolutionLabel(Resolutions[index]).c_str())) {
            return false;
        }
    }
    return AppendMenuW(toolbar.resolutions, MF_SEPARATOR, 0, nullptr) &&
           AppendMenuW(toolbar.resolutions, MF_STRING, CustomResolution, L"\u81ea\u5b9a\u4e49\u5206\u8fa8\u7387\u2026") &&
           AppendMenuW(toolbar.menu, MF_STRING, Disconnect, L"\u65ad\u5f00\u8fde\u63a5");
}

}

/// Collect a local resolution after the SDL session has paused remote input.
bool promptResolution(SDL_Window* video, int& width, int& height)
{
    HWND owner = nativeWindow(video);
    Resolution size = {width, height};
    if (!owner || !editResolution(owner, size)) {
        return false;
    }
    width = size.width;
    height = size.height;
    return true;
}

/// Attach the native menu to the video window without changing its renderer.
bool attach(SDL_Window* video, Uint32 eventType, bool absolute, bool cursorVisible, int width, int height)
{
    HWND window = nativeWindow(video);
    if (!window || GetMenu(window) || getToolbar(window)) {
        return false;
    }

    auto toolbar = std::make_unique<Toolbar>();
    toolbar->video = video;
    toolbar->eventType = eventType;
    toolbar->menu = CreateMenu();
    if (!toolbar->menu) {
        return false;
    }

    if (!populateMenu(*toolbar) || !SetMenu(window, toolbar->menu)) {
        DestroyMenu(toolbar->menu);
        return false;
    }
    if (!SetWindowSubclass(window, toolbarWindowProc, ToolbarSubclass,
                           reinterpret_cast<DWORD_PTR>(toolbar.get()))) {
        SetMenu(window, nullptr);
        DestroyMenu(toolbar->menu);
        return false;
    }
    toolbar.release();
    update(video, absolute, cursorVisible, width, height);
    return true;
}

/// Reflect changes from either the native menu or Moonlight's keyboard shortcuts.
void update(SDL_Window* video, bool absolute, bool cursorVisible, int width, int height)
{
    HWND window = nativeWindow(video);
    auto* toolbar = getToolbar(window);
    if (!toolbar) {
        return;
    }
    toolbar->absolute = absolute;
    toolbar->size = {width, height};
    CheckMenuRadioItem(toolbar->settings, AbsoluteMouse, RelativeMouse,
                       absolute ? AbsoluteMouse : RelativeMouse, MF_BYCOMMAND);
    EnableMenuItem(toolbar->settings, LocalCursor, MF_BYCOMMAND | (absolute ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(toolbar->settings, LocalCursor, MF_BYCOMMAND | (cursorVisible ? MF_CHECKED : MF_UNCHECKED));
    const auto current = L"\u5f53\u524d\uff1a" + resolutionLabel(toolbar->size);
    ModifyMenuW(toolbar->resolutions, CurrentResolution, MF_BYCOMMAND | MF_STRING | MF_GRAYED,
                CurrentResolution, current.c_str());
    for (UINT index = 0; index < std::size(Resolutions); ++index) {
        const auto& size = Resolutions[index];
        const bool selected = width == size.width && height == size.height;
        CheckMenuItem(toolbar->resolutions, ResolutionBase + index,
                      MF_BYCOMMAND | (selected ? MF_CHECKED : MF_UNCHECKED));
    }
    DrawMenuBar(window);
}

/// Detach and destroy menus before Moonlight destroys its SDL video window.
void detach(SDL_Window* video)
{
    HWND window = nativeWindow(video);
    auto* toolbar = getToolbar(window);
    if (!toolbar) {
        return;
    }
    RemoveWindowSubclass(window, toolbarWindowProc, ToolbarSubclass);
    if (SetMenu(window, nullptr)) {
        DestroyMenu(toolbar->menu);
    }
    delete toolbar;
}

}
