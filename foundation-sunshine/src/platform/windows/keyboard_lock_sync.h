// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#pragma once

#include <windows.h>
#include <thread>

namespace platf::keyboard_locks {
  constexpr unsigned capability = 0x40000000;
  constexpr unsigned char packet_flag = 0x80;

  // Bits: Caps Lock = 1, Num Lock = 2, Scroll Lock = 4.
  inline unsigned read_state() {
    return ((GetKeyState(VK_CAPITAL) & 1) ? 1u : 0u) |
           ((GetKeyState(VK_NUMLOCK) & 1) ? 2u : 0u) |
           ((GetKeyState(VK_SCROLL) & 1) ? 4u : 0u);
  }

  inline unsigned make_inputs(unsigned current, unsigned desired, INPUT (&events)[6]) {
    const WORD keys[] = {VK_CAPITAL, VK_NUMLOCK, VK_SCROLL};
    unsigned count = 0;
    for (unsigned bit = 0; bit < 3; ++bit) {
      if (!((current ^ desired) & (1u << bit))) continue;
      INPUT press{};
      press.type = INPUT_KEYBOARD;
      press.ki.wVk = keys[bit];
      if (keys[bit] == VK_NUMLOCK) press.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
      events[count++] = press;
      press.ki.dwFlags |= KEYEVENTF_KEYUP;
      events[count++] = press;
    }
    return count;
  }

  inline bool synchronize(unsigned desired, DWORD *error_out = nullptr) {
    if (error_out) *error_out = ERROR_SUCCESS;
    if (desired & ~7u) {
      if (error_out) *error_out = ERROR_INVALID_PARAMETER;
      return false;
    }
    bool success = false;
    DWORD error = ERROR_SUCCESS;
    // A fresh input queue snapshots the current desktop key state. Reusing a
    // worker's GetKeyState snapshot would toggle keys again on periodic updates.
    std::thread worker([&] {
      // Match Sunshine's existing syncThreadDesktop() access rights. A handle
      // opened with only READOBJECTS | WRITEOBJECTS can attach successfully but
      // makes SendInput fail, so packet-only tests cannot validate this path.
      HDESK desktop = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
      if (!desktop) { error = GetLastError(); return; }
      HDESK previous = GetThreadDesktop(GetCurrentThreadId());
      if (SetThreadDesktop(desktop)) {
        MSG message;
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        // Don't synthesize an up while a physical lock key is held.
        if (!(GetAsyncKeyState(VK_CAPITAL) & 0x8000) &&
            !(GetAsyncKeyState(VK_NUMLOCK) & 0x8000) &&
            !(GetAsyncKeyState(VK_SCROLL) & 0x8000)) {
          INPUT events[6]{};
          unsigned count = make_inputs(read_state(), desired, events);
          SetLastError(ERROR_SUCCESS);
          success = count == 0 || SendInput(count, events, sizeof(INPUT)) == count;
          if (!success) error = GetLastError();
        } else {
          error = ERROR_BUSY;
        }
        SetThreadDesktop(previous);
      } else {
        error = GetLastError();
      }
      CloseDesktop(desktop);
    });
    worker.join();
    if (error_out) *error_out = error;
    return success;
  }
}
