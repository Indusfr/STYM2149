// STYM2149 — plugin: layout of the system keyboard, for Auto in Options > Keyboard layout (D-215)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Kept apart from the JUCE sources: each system's own header (windows.h, Carbon, Xlib) is included here only.
#include "KeyboardLayout.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <Carbon/Carbon.h>
#elif defined(__linux__)
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#endif

namespace stym::plugin {

std::string layoutOfKeys(char32_t atQ, char32_t atY) {
    auto lower = [](char32_t c) { return c >= U'A' && c <= U'Z' ? c + 0x20 : c; };
    if (lower(atQ) == U'a') return "azerty";
    if (lower(atY) == U'z') return "qwertz";
    return "qwerty";
}

#if defined(_WIN32)

std::string systemKeyboardLayout() {
    // Layout of the thread of the window in front (the host's or the application's), otherwise of this thread; scan
    // codes 0x10 and 0x15 are the places of Q and Y.
    const HWND front = GetForegroundWindow();
    const HKL layout = GetKeyboardLayout(front ? GetWindowThreadProcessId(front, nullptr) : 0);
    // The virtual-key code of a letter key is its capital ASCII letter.
    auto charAt = [layout](UINT scan) -> char32_t {
        const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK, layout);
        return vk >= 'A' && vk <= 'Z' ? char32_t(vk) : 0;
    };
    return layoutOfKeys(charAt(0x10), charAt(0x15));
}

#elif defined(__APPLE__)

std::string systemKeyboardLayout() {
    // Not compiled on the project's machines (no macOS runner): TO CONFIRM at the first macOS build.
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source) return "qwerty";
    char32_t q = 0, y = 0;
    const auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    if (data) {
        const auto* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
        // Character the key shows without modifiers (kUCKeyActionDisplay, modifier state 0), dead keys ignored.
        auto charAt = [layout](UInt16 key) -> char32_t {
            UInt32 dead = 0;
            UniChar text[4];
            UniCharCount length = 0;
            if (UCKeyTranslate(layout, key, kUCKeyActionDisplay, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
                               &dead, 4, &length, text) != noErr ||
                length == 0)
                return 0;
            return char32_t(text[0]);
        };
        q = charAt(kVK_ANSI_Q);
        y = charAt(kVK_ANSI_Y);
    }
    CFRelease(source);
    return layoutOfKeys(q, y);
}

#elif defined(__linux__)

std::string systemKeyboardLayout() {
    // Xlib loaded at run time, as JUCE does: keycodes 24 and 29 are the places of Q and Y (evdev), in the current
    // group of the keyboard.
    void* x11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!x11) return "qwerty";
    using OpenDisplay = Display* (*)(const char*);
    using CloseDisplay = int (*)(Display*);
    using GetState = Status (*)(Display*, unsigned int, XkbStatePtr);
    using KeycodeToKeysym = KeySym (*)(Display*, KeyCode, int, int);
    const auto open = reinterpret_cast<OpenDisplay>(dlsym(x11, "XOpenDisplay"));
    const auto close = reinterpret_cast<CloseDisplay>(dlsym(x11, "XCloseDisplay"));
    const auto state = reinterpret_cast<GetState>(dlsym(x11, "XkbGetState"));
    const auto keysym = reinterpret_cast<KeycodeToKeysym>(dlsym(x11, "XkbKeycodeToKeysym"));
    std::string result = "qwerty";
    if (open && close && state && keysym) {
        if (Display* display = open(nullptr)) {
            XkbStateRec s{};
            const int group = state(display, XkbUseCoreKbd, &s) == Success ? int(s.group) : 0;
            // Keysym of the key in that group at shift level 0; the keysyms of the Latin letters are their ASCII codes.
            auto charAt = [&](KeyCode code) -> char32_t {
                const KeySym k = keysym(display, code, group, 0);
                return k >= XK_a && k <= XK_z ? char32_t(k) : k >= XK_A && k <= XK_Z ? char32_t(k) : 0;
            };
            result = layoutOfKeys(charAt(24), charAt(29));
            close(display);
        }
    }
    dlclose(x11);
    return result;
}

#else

std::string systemKeyboardLayout() { return "qwerty"; }

#endif

}  // namespace stym::plugin
