// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#include "moonlight-qt/app/streaming/focusime_protocol.h"
#include <cassert>
#include <iostream>
using namespace focusime;
int main() {
    Mode decoded = Mode::unknown;
    for (unsigned i = 0; i < 256; ++i) {
        const auto packet = std::string("FIM2") + char(i);
        assert(decode(packet, decoded, '2') == (i < 3));
        assert(!decode(packet, decoded)); // v1 cannot interpret v2 mutations
    }
    for (auto packet : {std::string(), std::string("FIM2"), std::string("FIM2\1x", 6), std::string("FIM1\1", 5)})
        assert(!decode(packet, decoded, '2'));

    // Regression: local Chinese must survive repeated stale English replies.
    Mode local = Mode::chinese, host = Mode::english;
    for (int poll = 0; poll < 500; ++poll) {
        assert(requestedMode(false, local) == Mode::chinese);
        if (mayApplyRemote(false, false, host)) local = host;
        assert(local == Mode::chinese);
    }
    // Both actual local modes can be sent; no shortcut interpretation involved.
    for (Mode mode : {Mode::english, Mode::chinese}) {
        host = requestedMode(false, mode);
        assert(host == mode);
        assert(requestedMode(true, mode) == Mode::unknown);
        assert(mayApplyRemote(true, true, host));
        // Focus changed while the HTTPS reply was in flight: don't overwrite.
        assert(!mayApplyRemote(true, false, host));
        assert(!mayApplyRemote(false, true, host));
    }
    assert(requestedMode(false, Mode::unknown) == Mode::unknown);
    assert(!mayApplyRemote(true, true, Mode::unknown));
    std::cout << "PASS ImeSync2: local changes survive stale replies; both directions; focus races; unknown contexts; strict wire versions\n";
}
