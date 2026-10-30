// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#include "input.h"
#include "streaming/streamutils.h"
#include <Limelight.h>

#ifdef Q_OS_WIN32
#include <SDL_syswm.h>

namespace {
bool activate(HWND window) {
    if (!IsWindow(window)) return false;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    if (GetForegroundWindow() == window || SetForegroundWindow(window)) return true;
    // The shortcut is handled on a separate message thread. Temporarily join the
    // foreground input queue only after the shortcut chord has been released.
    DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD thisThread = GetCurrentThreadId();
    bool attached = foregroundThread != thisThread && AttachThreadInput(thisThread, foregroundThread, TRUE);
    bool success = SetForegroundWindow(window) != FALSE;
    if (attached) AttachThreadInput(thisThread, foregroundThread, FALSE);
    return success || GetForegroundWindow() == window;
}
}
#endif

void SdlInputHandler::syncKeyboardLockState()
{
#ifdef Q_OS_WIN32
    if (m_SyncKeyboardLocks && m_FocusBridge) {
        // Reserved VK 0, key-up only, explicit flag and negotiated capability.
        // No synthetic lock-key down/up is sent in addition to this state packet.
        LiSendKeyboardEvent2(0, KEY_ACTION_UP, char(m_FocusBridge->lockState() & 7), FocusLockStatePacketFlag);
    }
#endif
}

void SdlInputHandler::handleFocusEvent(const SDL_UserEvent& event)
{
#ifdef Q_OS_WIN32
    if (!m_Window || event.windowID != SDL_GetWindowID(m_Window)) return;
    if (event.code == FocusToggleEvent) toggleLocalFocus();
    else if (event.code == FocusLockStateEvent) syncKeyboardLockState();
#else
    Q_UNUSED(event);
#endif
}

void SdlInputHandler::toggleLocalFocus()
{
#ifdef Q_OS_WIN32
    if (!m_FocusSwitchEnabled || !m_FocusBridge || !m_Window) return;
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(m_Window, &info)) return;
    HWND stream = info.info.win.window;
    POINT point{};
    GetCursorPos(&point);

    if (!m_LocalFocus && GetForegroundWindow() == stream && isCaptureActive()) {
        // The last transmitted normalized absolute coordinates are authoritative;
        // SDL's relative mode cursor is only a local implementation detail.
        raiseAllKeys();
        for (int button = BUTTON_LEFT; button <= BUTTON_X2; ++button)
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);
        m_LocalFocus = true;
        setCaptureActive(false);
        SDL_CaptureMouse(SDL_FALSE);
        m_PendingMouseButtonsAllUpOnVideoRegionLeave = false;

        // On the first switch, prefer the other display if the pre-stream cursor
        // position was on the streaming display. Subsequent switches restore exactly.
        if (!m_LocalWindow) {
            int streamDisplay = SDL_GetWindowDisplayIndex(m_Window);
            SDL_Rect bounds{};
            SDL_GetDisplayBounds(streamDisplay, &bounds);
            if (SDL_PointInRect(&m_LocalMouse, &bounds) && SDL_GetNumVideoDisplays() > 1) {
                int other = streamDisplay == 0 ? 1 : 0;
                SDL_GetDisplayBounds(other, &bounds);
                m_LocalMouse = {bounds.x + bounds.w / 2, bounds.y + bounds.h / 2};
            }
        }
        // Clamp to a still-connected monitor after display topology changes.
        POINT local{m_LocalMouse.x, m_LocalMouse.y};
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromPoint(local, MONITOR_DEFAULTTONEAREST), &monitor);
        local.x = qBound(monitor.rcMonitor.left, local.x, monitor.rcMonitor.right - 1);
        local.y = qBound(monitor.rcMonitor.top, local.y, monitor.rcMonitor.bottom - 1);
        HWND target = static_cast<HWND>(m_LocalWindow);
        if (!IsWindow(target) || target == stream) target = GetAncestor(WindowFromPoint(local), GA_ROOT);
        if (target != stream) activate(target);
        SetCursorPos(local.x, local.y);
        m_FocusBridge->flash(local.x, local.y);
    }
    else {
        m_LocalMouse = {int(point.x), int(point.y)};
        HWND previous = GetForegroundWindow();
        if (previous != stream) m_LocalWindow = previous;
        if (!activate(stream)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unable to focus streaming window; retaining local focus");
            return;
        }
        m_RestoringFocus = true;
        m_LocalFocus = false;
        int width, height;
        SDL_GetWindowSize(m_Window, &width, &height);
        SDL_Rect src{0, 0, m_StreamWidth, m_StreamHeight}, dst{0, 0, width, height};
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);
        int x = m_AbsoluteMouseMode ? dst.x + qBound(0, qRound(m_RemoteMouse.x() * dst.w), dst.w - 1) : width / 2;
        int y = m_AbsoluteMouseMode ? dst.y + qBound(0, qRound(m_RemoteMouse.y() * dst.h), dst.h - 1) : height / 2;
        SDL_WarpMouseInWindow(m_Window, x, y);
        SDL_PumpEvents();
        SDL_FlushEvent(SDL_MOUSEMOTION);
        setCaptureActive(true);
        SDL_PumpEvents();
        SDL_FlushEvent(SDL_MOUSEMOTION);
        syncKeyboardLockState();
        POINT flash{x, y};
        ClientToScreen(stream, &flash);
        m_FocusBridge->flash(flash.x, flash.y);
        m_RestoringFocus = false;
    }
#endif
}
