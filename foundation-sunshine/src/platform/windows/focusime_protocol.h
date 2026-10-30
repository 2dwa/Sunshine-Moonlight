// Modified 2026-10-30 for ImeSync2; contributor: GPT-6 Astra (AI-assisted).
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
namespace focusime {
enum class Mode : unsigned char { unknown = 0, english = 1, chinese = 2 };
inline bool known(Mode mode) { return mode == Mode::english || mode == Mode::chinese; }
inline std::string encode(Mode mode, char version = '1') {
    return std::string("FIM") + version + char(mode);
}
inline bool decode(const std::string& bytes, Mode& mode, char version = '1') {
    if (bytes.size() != 5 || bytes.compare(0, 3, "FIM") != 0 || bytes[3] != version ||
        static_cast<unsigned char>(bytes[4]) > 2) return false;
    mode = static_cast<Mode>(bytes[4]);
    return true;
}
// A local app is authoritative while it owns focus. Unknown input contexts
// must never be converted into a guessed English command.
inline Mode requestedMode(bool streamFocused, Mode local) {
    return !streamFocused && known(local) ? local : Mode::unknown;
}
// A response started while local focus was active is only an acknowledgement.
// Recheck focus after HTTP completes: a stale reply must not undo a local edit.
inline bool mayApplyRemote(bool streamAtRequest, bool streamAtReply, Mode remote) {
    return streamAtRequest && streamAtReply && known(remote);
}
}
