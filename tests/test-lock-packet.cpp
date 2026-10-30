// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" {
#include <moonlight-common-c/src/Input.h>
}
#include "foundation-sunshine/src/keyboard_lock_packet.h"

int main() {
    unsigned checks = 0;
    for (unsigned flags = 0; flags < 256; ++flags) {
        for (unsigned state = 0; state < 256; ++state) {
            for (unsigned kind = 0; kind < 3; ++kind) {
                NV_KEYBOARD_PACKET packet{};
                // Copy bytes into the actual upstream packed wire structure.
                const unsigned char flag_byte = static_cast<unsigned char>(flags);
                const unsigned char state_byte = static_cast<unsigned char>(state);
                std::memcpy(&packet.flags, &flag_byte, 1);
                std::memcpy(&packet.modifiers, &state_byte, 1);
                packet.keyCode = kind == 2 ? 0x14 : 0;
                packet.header.magic = kind == 1 ? KEY_DOWN_EVENT_MAGIC : KEY_UP_EVENT_MAGIC;
                bool accepted = input::keyboard_locks::valid_packet(
                    packet.flags, packet.keyCode, packet.header.magic == KEY_UP_EVENT_MAGIC,
                    static_cast<unsigned char>(packet.modifiers));
                bool expected = flags == 0x80 && state < 8 && kind == 0;
                if (accepted != expected) {
                    std::fprintf(stderr, "FAIL: wire flags=0x%02x char=%d state=%u kind=%u accepted=%d expected=%d\n",
                                 flags, int(packet.flags), state, kind, accepted, expected);
                    return 1;
                }
                ++checks;
            }
        }
    }
    std::printf("PASS: %u wire packet cases; all 8 lock states accepted, malformed packets rejected.\n", checks);
}
