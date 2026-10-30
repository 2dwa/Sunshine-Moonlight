// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace input::keyboard_locks {
  // Input.h stores wire octets as char. Normalize before comparing high bits.
  inline bool valid_packet(unsigned char flags, short key_code, bool key_up, unsigned char state) {
    return flags == 0x80 && key_code == 0 && key_up && (state & ~7u) == 0;
  }
}
