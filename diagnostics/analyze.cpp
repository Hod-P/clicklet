// SPDX-License-Identifier: MIT
#include "analyze.h"
#include <cstdlib>

static latency::HookTrace hooks;
static latency::ReceiverTrace receiver;
int wmain(int argc, WCHAR **argv)
{
    if (argc != 3 && argc != 4)
    {
        std::fprintf(stderr, "Usage: analyze.exe HOOK.bin RECEIVER.bin [candidate_window_ms=20,1..1000]\n");
        return 2;
    }
    UINT window = 20;
    if (argc == 4)
    {
        WCHAR *end = nullptr;
        const unsigned long parsed = wcstoul(argv[3], &end, 10);
        if (end == argv[3] || *end || parsed == 0 || parsed > 1000) return 2;
        window = static_cast<UINT>(parsed);
    }
    if (!latency::Load(hooks, argv[1]) || !latency::Load(receiver, argv[2]))
    { std::fprintf(stderr, "FAIL: invalid, truncated, oversized or incompatible trace file.\n"); return 2; }
    std::printf("run=%s scenario=%s frequency=%lld candidate_window_ms=%u\n",
                hooks.header.run, hooks.header.scenario, hooks.header.frequency, window);
    const auto analysis = latency::Analyze(hooks, receiver, window);
    latency::Print(analysis, hooks, receiver);
    return analysis.Clean(hooks.header, receiver.header) ? 0 : 1;
}
