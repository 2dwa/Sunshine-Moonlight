// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#pragma once

// Shared by the SDL input path and standalone regression tests. Waiting for
// the full chord's release prevents held modifiers leaking to the other PC.
struct FocusShortcut {
    enum Result { Pass, Consume, Toggle };
    bool pending = false;
    bool primaryDown = false;
    Result process(int key, bool pressed, bool repeat, int modifiers, int wantedKey, int wantedModifiers) {
        bool primary = key == wantedKey;
        if (primary && pressed && !repeat && modifiers == wantedModifiers) {
            pending = primaryDown = true;
            return Consume;
        }
        if (!pending) return Pass;
        if (primary && !pressed) primaryDown = false;
        if (!primaryDown && modifiers == 0) {
            pending = false;
            return Toggle;
        }
        return primary ? Consume : Pass;
    }
};
