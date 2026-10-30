// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#include "foundation-sunshine/src/remote_usb/import_request.h"
#include <cassert>
#include <iostream>
#include <string>
int main() {
  std::string packet("\x01\x11\x80\x03\0\0\0\0", 8);
  packet += "1-2"; packet.resize(40, 0);
  assert(remote_usb::selected_import(packet, "1-2"));
  assert(!remote_usb::selected_import(packet, "1-3"));
  assert(!remote_usb::selected_import(packet, ""));
  for (int i=0;i<40;++i) assert(!remote_usb::selected_import(packet.substr(0,i), "1-2"));
  for (int i=0;i<40;++i) {
    auto changed=packet; changed[i]^=0x20;
    assert(!remote_usb::selected_import(changed, "1-2"));
  }
  assert(remote_usb::selected_import(packet + "tail", "1-2"));
  std::cout << "PASS: selected USB import header, truncation, busid and padding checks\n";
}
