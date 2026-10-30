// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
// Transport integration fixture; never opens a physical USB device.
#include "src/remote_usb/reverse_tunnel_service.h"
#include "src/logging_severity.h"
#include <openssl/pem.h>
#include <iostream>
#include <fstream>
boost::log::sources::severity_logger<int> verbose, debug, info, warning, error, fatal;
int main(int argc, char** argv) {
  if (argc != 6) return 2;
  FILE* file = fopen(argv[3], "rb");
  if (!file) return 2;
  X509* expected = PEM_read_X509(file, nullptr, nullptr, nullptr); fclose(file);
  if (!expected) return 2;
  remote_usb::reverse_tunnel_config config;
  config.bind_address = "127.0.0.1"; config.port = 0;
  config.exporter_port = static_cast<uint16_t>(std::stoi(argv[5]));
  config.certificate_file = argv[1]; config.private_key_file = argv[2];
  config.verify_client_cert = [expected](X509* peer) { return X509_cmp(peer, expected) == 0; };
  config.session_token = std::string(64, 'a');
  remote_usb::reverse_tunnel_service service;
  if (!service.start(config)) return 3;
  std::cout << "PORT " << service.bound_port() << std::endl;
  for (int i = 0; i < 3000 && !std::ifstream(argv[4]); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  service.stop(); X509_free(expected);
}
