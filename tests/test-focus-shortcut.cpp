// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#include "moonlight-qt/app/streaming/input/focusshortcut.h"
#include <cassert>
#include <cstdio>
int main() {
    using F = FocusShortcut;
    // Alt+Q, key first then modifier release, including auto-repeat.
    F a;
    assert(a.process(0, true, false, 2, 'Q', 2) == F::Pass);
    assert(a.process('Q', true, false, 2, 'Q', 2) == F::Consume);
    assert(a.process('Q', true, true, 2, 'Q', 2) == F::Consume);
    assert(a.process('Q', false, false, 2, 'Q', 2) == F::Consume);
    assert(a.process(0, false, false, 0, 'Q', 2) == F::Toggle);
    assert(a.process(0, false, false, 0, 'Q', 2) == F::Pass);
    // Ctrl+Alt+F12: modifier first, then primary release, exactly one toggle.
    F b;
    assert(b.process(0x7b, true, false, 3, 0x7b, 3) == F::Consume);
    assert(b.process(0, false, false, 2, 0x7b, 3) == F::Pass);
    assert(b.process(0, false, false, 0, 0x7b, 3) == F::Pass);
    assert(b.process(0x7b, false, false, 0, 0x7b, 3) == F::Toggle);
    // Do not steal ordinary typing, another modifier combination or key-up.
    F c;
    assert(c.process('Q', true, false, 0, 'Q', 2) == F::Pass);
    assert(c.process('Q', true, false, 6, 'Q', 2) == F::Pass);
    assert(c.process('Q', false, false, 2, 'Q', 2) == F::Pass);
    std::puts("PASS: captured Alt+Q; Ctrl+Alt+F12; both release orders; repeat suppression; non-shortcut pass-through.");
}
