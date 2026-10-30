// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#include "foundation-sunshine/src/platform/windows/console_startup.h"
#include <iostream>
int main(int argc, char**) {
    sunshine_console::prepare(argc);
    if (GetConsoleWindow() != nullptr) return 1;
    std::cout << "stdout-preserved\n";
    std::cerr << "stderr-preserved\n";
    return 0;
}
