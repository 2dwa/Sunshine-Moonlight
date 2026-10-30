// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "foundation-sunshine/src/platform/windows/keyboard_lock_sync.h"
#include <cstdio>
#include <cassert>

int main() {
    for (unsigned from = 0; from != 8; ++from) {
        for (unsigned to = 0; to != 8; ++to) {
            INPUT events[6]{};
            unsigned count = platf::keyboard_locks::make_inputs(from, to, events);
            unsigned state = from;
            assert(count <= 6 && count % 2 == 0);
            for (unsigned i = 0; i < count; i += 2) {
                assert(events[i].type == INPUT_KEYBOARD);
                assert(events[i].ki.wVk == events[i+1].ki.wVk);
                assert(!(events[i].ki.dwFlags & KEYEVENTF_KEYUP));
                assert(events[i+1].ki.dwFlags & KEYEVENTF_KEYUP);
                unsigned bit = events[i].ki.wVk == VK_CAPITAL ? 1 : events[i].ki.wVk == VK_NUMLOCK ? 2 : 4;
                assert((from ^ to) & bit);
                state ^= bit;
            }
            assert(state == to);
            assert((from != to) || count == 0);
        }
    }
    assert(!platf::keyboard_locks::synchronize(8));
    std::puts("PASS: all 64 lock-state transitions; balanced input; idempotence; invalid mask rejected.");
}
