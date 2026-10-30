// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <imm.h>
#include <string>

namespace focusime {
constexpr unsigned capability = 0x10000000;
enum class Mode : unsigned char { unknown = 0, english = 1, chinese = 2 };

inline std::string encode(Mode mode) {
    return std::string("FIM1") + char(mode);
}
inline bool decode(const std::string& bytes, Mode& mode) {
    if (bytes.size() != 5 || bytes.compare(0, 4, "FIM1") != 0 ||
        static_cast<unsigned char>(bytes[4]) > 2) return false;
    mode = static_cast<Mode>(bytes[4]);
    return true;
}
struct Target {
    HWND root = nullptr;
    HWND focus = nullptr;
    HWND ime = nullptr;
    DWORD process = 0;
    DWORD thread = 0;
    HKL layout = nullptr;
    Mode mode = Mode::unknown;
};
inline bool control(HWND window, WPARAM command, LPARAM value, DWORD_PTR& result) {
    result = 0;
    return window && SendMessageTimeoutW(window, WM_IME_CONTROL, command, value,
                                         SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result) != 0;
}
inline Target inspect(HWND root, HWND previousFocus = nullptr) {
    Target target;
    if (!root || !IsWindow(root)) return target;
    target.root = root;
    target.thread = GetWindowThreadProcessId(root, &target.process);
    GUITHREADINFO info{sizeof(info)};
    if (!target.thread || !GetGUIThreadInfo(target.thread, &info)) return target;
    target.focus = info.hwndFocus;
    if (!target.focus && previousFocus && IsWindow(previousFocus) &&
        GetWindowThreadProcessId(previousFocus, nullptr) == target.thread) target.focus = previousFocus;
    if (!target.focus || GetAncestor(target.focus, GA_ROOT) != root) return target;
    target.layout = GetKeyboardLayout(target.thread);
    auto language = LOWORD(reinterpret_cast<ULONG_PTR>(target.layout));
    if (PRIMARYLANGID(language) == LANG_ENGLISH) {
        target.mode = Mode::english;
        return target;
    }
    // This extension intentionally supports Simplified Chinese Microsoft Pinyin.
    // Unknown/disabled input contexts (including SDL and password fields) must
    // never be mistaken for English and propagated to another application.
    if (language != MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) return target;
    target.ime = ImmGetDefaultIMEWnd(target.focus);
    DWORD_PTR opened = 0, conversion = 0;
    if (control(target.ime, 5 /* IMC_GETOPENSTATUS */, 0, opened) && opened &&
        control(target.ime, 1 /* IMC_GETCONVERSIONMODE */, 0, conversion)) {
        target.mode = conversion & IME_CMODE_NATIVE ? Mode::chinese : Mode::english;
    }
    return target;
}
inline Target foreground() {
    auto window = GetForegroundWindow();
    auto result = inspect(window);
    return GetForegroundWindow() == window ? result : Target{};
}
inline bool apply(const Target& original, Mode desired) {
    if (desired != Mode::english && desired != Mode::chinese) return false;
    auto target = inspect(original.root, original.focus);
    // A recycled HWND or focus change must not redirect a pending update.
    if (!target.focus || target.process != original.process || target.thread != original.thread ||
        target.focus != original.focus) return false;
    if (target.mode == desired) return true;
    if (target.mode == Mode::unknown) return false;
    if (!target.ime && desired == Mode::chinese) {
        // Only select an already-installed Chinese layout, never install one or
        // change the user's default language/Shift bindings. Retry on next poll.
        HKL layouts[64]{};
        int count = GetKeyboardLayoutList(64, layouts);
        for (int i = 0; i < count; ++i) {
            if (LOWORD(reinterpret_cast<ULONG_PTR>(layouts[i])) == 0x0804) {
                PostMessageW(target.focus, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(layouts[i]));
                break;
            }
        }
        return false;
    }
    DWORD_PTR result = 0;
    // Microsoft Pinyin English is exactly zero. Clearing NATIVE from 0x401
    // produces 0x400, which modern Pinyin silently ignores. Do not synthesize
    // Shift: that depends on the user's shortcut settings and can affect apps.
    DWORD conversion = desired == Mode::chinese ? IME_CMODE_NATIVE | IME_CMODE_SYMBOL : 0;
    if (!control(target.ime, 2 /* IMC_SETCONVERSIONMODE */, conversion, result)) return false;
    auto after = inspect(target.root, target.focus);
    return after.focus == target.focus && after.process == target.process && after.mode == desired;
}
} // namespace focusime
