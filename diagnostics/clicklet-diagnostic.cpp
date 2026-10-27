// SPDX-License-Identifier: MIT
#define wWinMain ClickletApplicationMain
#include "instrumented.h"
#undef wWinMain

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "DIAGNOSTIC ONLY: installs a real hook. Approval required.\n"
                     "Usage: clicklet-diagnostic.exe RUN_ID SCENARIO\n");
        return 2;
    }
    // Commit/touch every capture page before installing the hook. No lazy allocation in callback.
    ZeroMemory(&latency::capture, sizeof(latency::capture));
    latency::Prefault(&latency::capture, sizeof(latency::capture));
    if (!latency::Initialize(latency::capture.header, latency::kHookFile, argv[1], argv[2])) return 2;
    WCHAR path[MAX_PATH];
    if (!latency::OutputPath(path, latency::capture.header, L"hook") ||
        GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return 2;
    std::printf("Diagnostic capture armed: %s / %s. Exit from tray to save.\n", argv[1], argv[2]);
    const int result = ClickletApplicationMain(GetModuleHandleW(nullptr), nullptr,
                                              const_cast<LPWSTR>(L""), SW_HIDE);
    // The application has uninstalled its hook. A failed unhook cannot reenter without pumping.
    latency::capture.header.end = latency::Now();
    if (!latency::Save(latency::capture, path))
    {
        std::fprintf(stderr, "FAILED saving diagnostic trace; no result may be claimed.\n");
        return 3;
    }
    std::printf("Saved hook=%u send=%u dropped=%u/%u, application exit=%d\n",
                latency::capture.header.count, latency::capture.header.sends,
                latency::capture.header.dropped, latency::capture.header.sendDropped, result);
    return result;
}
