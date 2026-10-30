// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#pragma once

#include <QtGlobal>
#ifdef Q_OS_WIN32
#include <SDL.h>
#include <atomic>
#include <memory>

// Private extension shared with the accompanying Foundation Sunshine patch.
constexpr unsigned int FocusLockStateCapability = 0x40000000;
constexpr char FocusLockStatePacketFlag = char(0x80);
constexpr int FocusToggleEvent = 105;
constexpr int FocusLockStateEvent = 106;

class FocusBridge {
public:
    FocusBridge(unsigned int windowId, int key, int modifiers, bool syncLocks);
    ~FocusBridge();
    bool ready() const;
    void refreshHook();
    void flash(int x, int y);
    unsigned int lockState() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
#endif
