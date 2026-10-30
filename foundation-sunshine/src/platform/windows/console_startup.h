// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#pragma once
#include <windows.h>
#include <cstdio>
#include <io.h>

namespace sunshine_console {
inline bool has_stream(FILE* stream) {
    const int fd = _fileno(stream);
    return fd >= 0 && _get_osfhandle(fd) != -1 && _get_osfhandle(fd) != -2;
}

inline void prepare(int argc) {
    // Normal GUI/service startup must never allocate or hide a console. In
    // particular, do not hide a terminal that belongs to the launching user.
    if (argc <= 1 || (has_stream(stdout) && has_stream(stderr))) return;
    // Explicit command-line invocations can reuse an existing parent terminal.
    // Never call AllocConsole(), and retain redirected pipes/files unchanged.
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    if (!has_stream(stdout)) std::freopen("CONOUT$", "w", stdout);
    if (!has_stream(stderr)) std::freopen("CONOUT$", "w", stderr);
}
}
