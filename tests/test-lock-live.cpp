// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
// Opt-in live Win32 regression: briefly owns a test window and restores locks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "foundation-sunshine/src/platform/windows/keyboard_lock_sync.h"
#include <imm.h>
#include <cstdio>
#include <atomic>
#include <cstring>
constexpr UINT ReadState = WM_APP + 1;
static HWND window;
static std::atomic<int> result{0};
static std::atomic<unsigned> last_char{0};
LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (m == ReadState) return platf::keyboard_locks::read_state();
    if (m == WM_CHAR) { last_char = unsigned(a); return 0; }
    if (m == WM_CLOSE) { DestroyWindow(w); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(w, m, a, b);
}
bool modifiers_held() {
    const int keys[] = {VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN};
    for (auto key : keys) if (GetAsyncKeyState(key) & 0x8000) return true;
    return false;
}
int main(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], "--run-interactive")) {
        puts("Run with --run-interactive on an idle desktop. Temporarily tests Caps/Num/Scroll and restores them.");
        return 2;
    }
    WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FocusLockInputTest"; RegisterClassW(&wc);
    window = CreateWindowW(wc.lpszClassName, L"Sunshine lock sync test (closes automatically)",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 120,
                           nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 2;
    HWND previous = GetForegroundWindow();
    // Keep this test window independent of the user's IME text composition.
    auto ime = ImmAssociateContext(window, nullptr);
    ShowWindow(window, SW_SHOW); SetForegroundWindow(window); SetFocus(window);
    std::thread worker([] {
        Sleep(200);
        if (GetForegroundWindow() != window || modifiers_held()) {
            puts("SKIP: no exclusive test focus or modifier held; no input injected");
            result = 2; PostMessageW(window, WM_CLOSE, 0, 0); return;
        }
        unsigned initial = unsigned(SendMessageW(window, ReadState, 0, 0));
        unsigned checks = 0;
        // Repeats verify that polling an unchanged desired state does not toggle.
        const unsigned masks[] = {1,1,0,0,2,2,0,4,4,0,7,7,0};
        for (auto mask : masks) {
            if (GetForegroundWindow() != window || modifiers_held()) { result = 3; break; }
            unsigned desired = initial ^ mask;
            DWORD error = 0;
            bool ok = platf::keyboard_locks::synchronize(desired, &error);
            Sleep(70);
            unsigned actual = unsigned(SendMessageW(window, ReadState, 0, 0));
            printf("desired=%u foreground_actual=%u synchronize=%d error=%lu\n", desired, actual, ok, error);
            if (!ok || actual != desired) { result = 1; break; }
            ++checks;
            if (GetForegroundWindow() != window || modifiers_held()) { result = 3; break; }
            last_char = 0;
            INPUT letter[2]{};
            letter[0].type = letter[1].type = INPUT_KEYBOARD;
            letter[0].ki.wVk = letter[1].ki.wVk = 'A';
            letter[1].ki.dwFlags = KEYEVENTF_KEYUP;
            if (SendInput(2, letter, sizeof(INPUT)) != 2) { result = 1; break; }
            Sleep(70);
            unsigned expected = desired & 1 ? 'A' : 'a';
            if (last_char != expected) {
                printf("FAIL: text actual=%u expected=%u\n", unsigned(last_char), expected);
                result = 1; break;
            }
            ++checks;
        }
        // Best-effort restoration also runs after an assertion/focus failure.
        bool restored = platf::keyboard_locks::synchronize(initial);
        Sleep(70);
        restored = restored && unsigned(SendMessageW(window, ReadState, 0, 0)) == initial;
        if (!restored) result = 1;
        printf("%s: %u live state/text checks; original locks restored=%d\n", result == 0 ? "PASS" : "FAIL", checks, restored);
        PostMessageW(window, WM_CLOSE, 0, 0);
    });
    MSG m; while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    worker.join();
    (void)ime; // The test window (and its detached IME association) is destroyed.
    if (IsWindow(previous)) SetForegroundWindow(previous);
    return result;
}
