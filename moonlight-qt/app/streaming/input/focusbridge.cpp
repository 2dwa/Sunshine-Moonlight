// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#include "focusbridge.h"
#ifdef Q_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL_syswm.h>
#include <thread>

namespace {
constexpr UINT RefreshHook = WM_APP + 10;
constexpr UINT Flash = WM_APP + 11;
constexpr UINT Stop = WM_APP + 12;
constexpr COLORREF Transparent = RGB(255, 0, 255);
}

struct FocusBridge::Impl {
    unsigned int windowId;
    int key, modifiers;
    bool syncLocks;
    std::atomic<unsigned int> locks{0};
    std::atomic<bool> installed{false};
    std::atomic<HWND> streamWindow{nullptr};
    HANDLE started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HWND window = nullptr;
    HHOOK hook = nullptr;
    std::thread thread;
    bool down[256]{};
    bool pending = false;
    bool swallowKey = false;
    ULONGLONG flashStart = 0;
    static thread_local Impl* self;

    void post(int code) {
        SDL_Event event{};
        event.type = SDL_USEREVENT;
        event.user.windowID = windowId;
        event.user.code = code;
        SDL_PushEvent(&event);
    }

    int currentModifiers() const {
        return ((down[VK_LCONTROL] || down[VK_RCONTROL]) ? 1 : 0) |
               ((down[VK_LMENU] || down[VK_RMENU]) ? 2 : 0) |
               ((down[VK_LSHIFT] || down[VK_RSHIFT]) ? 4 : 0) |
               ((down[VK_LWIN] || down[VK_RWIN]) ? 8 : 0);
    }

    static LRESULT CALLBACK keyboard(int code, WPARAM message, LPARAM param) {
        auto p = self;
        if (code != HC_ACTION || !p) return CallNextHookEx(nullptr, code, message, param);
        const auto& k = *reinterpret_cast<KBDLLHOOKSTRUCT*>(param);
        if (k.vkCode >= 256 || k.scanCode == 0x21d) return CallNextHookEx(nullptr, code, message, param);
        bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        bool repeat = p->down[k.vkCode];
        p->down[k.vkCode] = pressed;
        unsigned bit = k.vkCode == VK_CAPITAL ? 1 : k.vkCode == VK_NUMLOCK ? 2 : k.vkCode == VK_SCROLL ? 4 : 0;
        if (bit && pressed && !repeat) {
            p->locks.fetch_xor(bit);
            if (p->syncLocks) p->post(FocusLockStateEvent);
        }
        // SDL's keyboard grab consumes Alt/Ctrl/Win before older hooks see
        // them. While our stream is foreground, its SDL keyboard handler is
        // the sole shortcut authority. The global hook handles other apps.
        if (GetForegroundWindow() == p->streamWindow.load()) {
            p->pending = p->swallowKey = false;
            return CallNextHookEx(nullptr, code, message, param);
        }
        bool consume = p->swallowKey && int(k.vkCode) == p->key;
        if (pressed && !repeat && int(k.vkCode) == p->key && p->currentModifiers() == p->modifiers) {
            p->pending = p->swallowKey = true;
            consume = true;
        }
        if (!pressed && int(k.vkCode) == p->key) p->swallowKey = false;
        // Switch after releasing the complete chord so modifiers cannot leak to the new focus.
        if (p->pending && !p->down[p->key] && p->currentModifiers() == 0) {
            p->pending = false;
            p->post(FocusToggleEvent);
        }
        return consume ? 1 : CallNextHookEx(nullptr, code, message, param);
    }

    void installHook() {
        if (hook) UnhookWindowsHookEx(hook);
        for (int i = 0; i < 256; ++i) down[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
        hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard, GetModuleHandleW(nullptr), 0);
        installed = hook != nullptr;
        if (!hook) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Focus shortcut hook failed: %lu", GetLastError());
    }

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto p = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            p = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        }
        if (!p) return DefWindowProcW(hwnd, message, wp, lp);
        switch (message) {
        case RefreshHook: p->installHook(); return 0;
        case Flash:
            p->flashStart = GetTickCount64();
            SetWindowPos(hwnd, HWND_TOPMOST, int(wp), int(lp), 40, 48, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            SetTimer(hwnd, 1, 50, nullptr);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_TIMER:
            if (wp == 2) { if (p->syncLocks) p->post(FocusLockStateEvent); return 0; }
            if (GetTickCount64() - p->flashStart >= 500) {
                KillTimer(hwnd, 1);
                ShowWindow(hwnd, SW_HIDE);
            } else {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rect{0, 0, 40, 48};
            HBRUSH background = CreateSolidBrush(Transparent);
            FillRect(dc, &rect, background);
            DeleteObject(background);
            bool white = ((GetTickCount64() - p->flashStart) / 100) % 2 == 0;
            HBRUSH brush = CreateSolidBrush(white ? RGB(255,255,255) : RGB(0,0,0));
            HPEN pen = CreatePen(PS_SOLID, 2, white ? RGB(0,0,0) : RGB(255,255,255));
            auto oldBrush = SelectObject(dc, brush);
            auto oldPen = SelectObject(dc, pen);
            POINT arrow[] = {{1,1},{1,31},{9,23},{16,39},{22,36},{15,21},{28,21}};
            Polygon(dc, arrow, 7);
            SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
            DeleteObject(brush); DeleteObject(pen);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case Stop: DestroyWindow(hwnd); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }

    void run() {
        self = this;
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MoonlightFocusFlash";
        RegisterClassW(&wc);
        window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                 wc.lpszClassName, L"", WS_POPUP, 0, 0, 40, 48, nullptr, nullptr, wc.hInstance, this);
        if (window) {
            SetLayeredWindowAttributes(window, Transparent, 255, LWA_COLORKEY);
            for (int i = 0; i < 256; ++i) down[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
            locks = ((GetKeyState(VK_CAPITAL) & 1) ? 1 : 0) |
                    ((GetKeyState(VK_NUMLOCK) & 1) ? 2 : 0) |
                    ((GetKeyState(VK_SCROLL) & 1) ? 4 : 0);
            installHook();
            SetTimer(window, 2, 1000, nullptr);
        }
        SetEvent(started);
        if (!window) return;
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (hook) UnhookWindowsHookEx(hook);
        installed = false;
        self = nullptr;
    }
};
thread_local FocusBridge::Impl* FocusBridge::Impl::self = nullptr;

FocusBridge::FocusBridge(unsigned int windowId, int key, int modifiers, bool syncLocks) : d(new Impl) {
    d->windowId = windowId; d->key = key; d->modifiers = modifiers; d->syncLocks = syncLocks;
    d->thread = std::thread([this] { d->run(); });
    WaitForSingleObject(d->started, INFINITE);
}
FocusBridge::~FocusBridge() {
    if (d->window) PostMessageW(d->window, Stop, 0, 0);
    if (d->thread.joinable()) d->thread.join();
    CloseHandle(d->started);
}
bool FocusBridge::ready() const { return d->installed; }
void FocusBridge::refreshHook() {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    auto window = SDL_GetWindowFromID(d->windowId);
    if (window && SDL_GetWindowWMInfo(window, &info)) d->streamWindow = info.info.win.window;
    if (d->window) PostMessageW(d->window, RefreshHook, 0, 0);
}
void FocusBridge::flash(int x, int y) { if (d->window) PostMessageW(d->window, Flash, WPARAM(x), LPARAM(y)); }
unsigned int FocusBridge::lockState() const { return d->locks; }
#endif
