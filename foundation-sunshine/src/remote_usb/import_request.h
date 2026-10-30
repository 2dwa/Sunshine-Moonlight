// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#pragma once
#include <string_view>
namespace remote_usb {
// Restrict a host-export tunnel to the one device selected in Moonlight.
// USB/IP OP_REQ_IMPORT consists of an 8-byte network header and 32-byte busid.
inline bool selected_import(std::string_view bytes, std::string_view busid) {
  if (bytes.size() < 40 || busid.empty() || busid.size() > 31) return false;
  const unsigned char header[] = {1, 17, 128, 3, 0, 0, 0, 0};
  for (unsigned i = 0; i < 8; ++i)
    if (static_cast<unsigned char>(bytes[i]) != header[i]) return false;
  for (unsigned i = 0; i < 32; ++i)
    if (bytes[8 + i] != (i < busid.size() ? busid[i] : '\0')) return false;
  return true;
}
}
