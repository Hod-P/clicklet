// SPDX-License-Identifier: MIT
// Everything originating in Clicklet, including diagnostic dispatch, uses Win32-call stubs.
#define CLICKLET_DIAGNOSTIC_TEST
#define main Phase0HarnessMain
#include "phase0.cpp"
#undef main
#include "../diagnostics/analyze.h"

// An unchanged second source instance provides a genuine uninstrumented callback control.
// All its desktop APIs also resolve to the original recording/count-only stubs.
namespace uninstrumented
{
// Unique helper names prevent HWND/POINT argument-dependent lookup finding the first copy.
#define ComputeAboutLinkBand PlainComputeAboutLinkBand
#define PointIsOnAboutLink PlainPointIsOnAboutLink
#define LoadTrayIcon PlainLoadTrayIcon
#define SendInput phase0::SendStub
#define CallNextHookEx phase0::NextStub
#define SetWindowsHookExW phase0::InstallStub
#define UnhookWindowsHookEx phase0::UninstallStub
#define Shell_NotifyIconW phase0::TrayStub
#include "../Clicklet.cpp"
#undef SendInput
#undef CallNextHookEx
#undef SetWindowsHookExW
#undef UnhookWindowsHookEx
#undef Shell_NotifyIconW
#undef ComputeAboutLinkBand
#undef PointIsOnAboutLink
#undef LoadTrayIcon
}

namespace latencyTests
{
static latency::HookTrace hooks, loaded;
static latency::ReceiverTrace receiver, loadedReceiver;

static void Fixture()
{
    ZeroMemory(&hooks, sizeof(hooks));
    ZeroMemory(&receiver, sizeof(receiver));
    latency::Initialize(hooks.header, latency::kHookFile, "selftest", "fixture");
    latency::Initialize(receiver.header, latency::kReceiverFile, "selftest", "fixture");
    hooks.header.synthetic = receiver.header.synthetic = 1;
    hooks.header.frequency = receiver.header.frequency = 1000000;
    hooks.header.begin = 100;
    hooks.header.end = 2000000;
    receiver.header.begin = 1000;
    receiver.header.end = 1000000;
    hooks.header.count = hooks.header.sends = receiver.header.count = 4;
    for (UINT i = 0; i < 4; ++i)
    {
        const bool up = i >= 2;
        const WORD vk = static_cast<WORD>(i % 2 == 0 ? VK_F6 : VK_F7);
        const WORD scan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        auto &h = hooks.hooks[i];
        h.begin = 2000 + i * 1000;
        h.end = h.begin + 30;
        h.id = i + 1;
        h.button = i % 2 == 0 ? XBUTTON1 : XBUTTON2;
        h.message = up ? WM_XBUTTONUP : WM_XBUTTONDOWN;
        h.result = 1;
        auto &s = hooks.sends[i];
        s.begin = h.begin + 5;
        s.end = h.begin + 15;
        s.hook = h.id;
        s.requested = s.accepted = 1;
        s.keys[0] = { vk, scan, up ? KEYEVENTF_KEYUP : 0u, 0 };
        auto &r = receiver.events[i];
        r.time = h.begin + 50;
        r.message = up ? WM_KEYUP : WM_KEYDOWN;
        r.vk = vk;
        r.scan = scan;
        r.repeat = 1;
        r.previous = r.transition = up ? 1u : 0u;
        r.sourceOK = TRUE;
        r.device = IMDT_KEYBOARD;
        r.origin = IMO_INJECTED;
    }
}
static bool Correlation()
{
    Fixture();
    auto a = latency::Analyze(hooks, receiver);
    CHECK(a.Clean(hooks.header, receiver.header));
    CHECK(a.matched == 4 && a.callbacks[0][0][0] == 30 && a.sends[1][1][0] == 10);
    CHECK(a.delivery[0][0][0] == 50 && a.delivery[1][1][0] == 50);
    receiver.events[0].time = hooks.sends[0].begin; // receive before SendInput returns is legal
    CHECK(latency::Analyze(hooks, receiver).matched == 4);
    Fixture();
    receiver.events[0].time = hooks.sends[0].begin - 1;
    CHECK(latency::Analyze(hooks, receiver).matched == 4);
    receiver.events[0].time -= 1;
    CHECK(latency::Analyze(hooks, receiver).missing == 1);
    return true;
}
static bool MissingUnexpectedAndAmbiguous()
{
    Fixture();
    receiver.events[1] = receiver.events[2];
    receiver.events[2] = receiver.events[3];
    --receiver.header.count;
    auto a = latency::Analyze(hooks, receiver);
    CHECK(a.missing == 1 && a.matched == 3 && !a.Clean(hooks.header, receiver.header));
    Fixture();
    receiver.events[4] = receiver.events[3];
    ++receiver.events[4].time;
    ++receiver.header.count;
    a = latency::Analyze(hooks, receiver);
    CHECK(a.ambiguous == 1 && a.unexpected == 2 && a.matched == 3);
    Fixture();
    hooks.sends[4] = hooks.sends[0];
    hooks.sends[4].begin += 100;
    hooks.sends[4].end += 100;
    hooks.hooks[0].end += 200;
    ++hooks.header.sends;
    a = latency::Analyze(hooks, receiver);
    CHECK(a.ambiguous == 0); // second DOWN begins after reception: no candidate
    receiver.events[0].time += 200;
    a = latency::Analyze(hooks, receiver);
    CHECK(a.ambiguous == 2 && a.unexpected == 1); // two sends, one received event: reject both
    Fixture();
    receiver.events[3].time = hooks.sends[3].begin + 20001;
    CHECK(latency::Analyze(hooks, receiver).missing == 1);
    return true;
}
static bool SignedTimestampPolicy()
{
    // Same-tick hook/send entry makes the tolerated cross-process -1 tick
    // visible in the reported hook-to-message interval, not only at send entry.
    const LONGLONG offsets[] = { 0, 7, -1, -2, -10 };
    for (LONGLONG offset : offsets)
    {
        Fixture();
        hooks.sends[0].begin = hooks.hooks[0].begin;
        receiver.events[0].time = hooks.sends[0].begin + offset;
        const auto a = latency::Analyze(hooks, receiver);
        if (offset >= -1)
        {
            CHECK(a.Clean(hooks.header, receiver.header));
            CHECK(a.matched == 4 && a.matches[0] == 0);
            CHECK(a.delivery[0][0].size() == 1 && a.delivery[0][0][0] == offset);
            CHECK(latency::HookToMessageTicks(a.expected[0], receiver.events[0]) == offset);
            CHECK(a.oneTickEarly == (offset == -1 ? 1u : 0u));
        }
        else
        {
            CHECK(!a.Clean(hooks.header, receiver.header));
            CHECK(a.matched == 3 && a.matches[0] == -1);
            CHECK(a.missing == 1 && a.unexpected == 1 && a.delivery[0][0].empty());
            CHECK(a.oneTickEarly == 0);
        }
    }
    // The uncertainty count is relative to send entry: hook-to-message can
    // remain positive when hook entry preceded the early receipt by several ticks.
    Fixture();
    receiver.events[0].time = hooks.sends[0].begin - 1;
    auto a = latency::Analyze(hooks, receiver);
    CHECK(a.Clean(hooks.header, receiver.header) && a.oneTickEarly == 1);
    CHECK(a.delivery[0][0][0] == 4);
    // Cross-process tolerance never excuses reversed same-process brackets.
    Fixture();
    hooks.hooks[0].end = hooks.hooks[0].begin - 1;
    a = latency::Analyze(hooks, receiver);
    CHECK(a.invalid != 0 && !a.Clean(hooks.header, receiver.header));
    Fixture();
    hooks.sends[0].end = hooks.sends[0].begin - 1;
    a = latency::Analyze(hooks, receiver);
    CHECK(a.invalid != 0 && !a.Clean(hooks.header, receiver.header));
    return true;
}
static bool SharedAndShortcut()
{
    Fixture();
    hooks.header.sends = receiver.header.count = 2;
    hooks.sends[1] = hooks.sends[3];
    hooks.sends[1].keys[0].vk = VK_F6;
    hooks.sends[1].keys[0].scan = hooks.sends[0].keys[0].scan;
    receiver.events[1] = receiver.events[3];
    receiver.events[1].vk = VK_F6;
    receiver.events[1].scan = receiver.events[0].scan;
    auto a = latency::Analyze(hooks, receiver);
    CHECK(a.Clean(hooks.header, receiver.header) && a.matched == 2);
    CHECK(a.callbacks[1][0].size() == 1 && a.sends[1][0].empty());
    CHECK(a.callbacks[0][1].size() == 1 && a.delivery[0][1].empty());
    Fixture();
    hooks.header.count = hooks.header.sends = 1;
    hooks.sends[0].requested = hooks.sends[0].accepted = 4;
    for (UINT i = 0; i < 4; ++i)
    {
        const WORD vk = i == 0 || i == 3 ? VK_CONTROL : VK_RETURN;
        const WORD scan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        const bool up = i >= 2;
        hooks.sends[0].keys[i] = { vk, scan, up ? KEYEVENTF_KEYUP : 0u, 0 };
        receiver.events[i] = receiver.events[0];
        receiver.events[i].vk = vk;
        receiver.events[i].scan = scan;
        receiver.events[i].time = 2050 + i;
        receiver.events[i].message = up ? WM_KEYUP : WM_KEYDOWN;
        receiver.events[i].previous = receiver.events[i].transition = up ? 1u : 0u;
    }
    a = latency::Analyze(hooks, receiver);
    CHECK(a.Clean(hooks.header, receiver.header) && a.matched == 4);
    CHECK(a.delivery[0][0].size() == 2 && a.delivery[0][1].size() == 2);
    CHECK(a.singleKeySends[0][0].empty() && a.sends[0][0].size() == 1);
    return true;
}
static bool ProvenanceAndOrdering()
{
    for (UINT mode = 0; mode < 8; ++mode)
    {
        Fixture();
        auto &r = receiver.events[0];
        switch (mode)
        {
        case 0: r.origin = IMO_HARDWARE; break;
        case 1: r.sourceOK = FALSE; break;
        case 2: ++r.scan; break;
        case 3: r.extended = 1; break;
        case 4: r.extra = 123; break;
        case 5: r.vk = VK_F8; break;
        case 6: r.repeat = 2; break;
        case 7: r.message = WM_KEYUP; break;
        }
        auto a = latency::Analyze(hooks, receiver);
        CHECK(a.missing == 1 && a.unexpected == 1 && a.matched == 3);
    }
    Fixture();
    std::swap(receiver.events[0], receiver.events[1]);
    receiver.events[0].time = 4000;
    receiver.events[1].time = 4100;
    CHECK(latency::Analyze(hooks, receiver).ordering == 1);
    Fixture();
    receiver.header.focusLost = 1;
    CHECK(!latency::Analyze(hooks, receiver).Clean(hooks.header, receiver.header));
    receiver.header.focusLost = 0;
    receiver.header.dropped = 1;
    CHECK(!latency::Analyze(hooks, receiver).Clean(hooks.header, receiver.header));
    return true;
}
static bool FailedAndMalformed()
{
    Fixture();
    hooks.sends[0].accepted = 0;
    auto a = latency::Analyze(hooks, receiver);
    CHECK(a.failed == 1 && a.unexpected == 1 && a.sequence == 1);
    Fixture();
    hooks.sends[0].requested = 2;
    a = latency::Analyze(hooks, receiver);
    CHECK(a.failed == 1 && a.partial == 1);
    Fixture();
    strcpy_s(receiver.header.run, "other");
    CHECK(!latency::Analyze(hooks, receiver).compatible);
    Fixture();
    ++receiver.header.frequency;
    CHECK(!latency::Analyze(hooks, receiver).compatible);
    Fixture();
    receiver.header.end = hooks.header.end + 1;
    CHECK(!latency::Analyze(hooks, receiver).compatible);
    Fixture();
    hooks.sends[0].hook = latency::kCapacity + 1;
    CHECK(latency::Analyze(hooks, receiver).invalid == 1);
    Fixture();
    hooks.header.count = latency::kCapacity + 1;
    CHECK(!latency::Analyze(hooks, receiver).compatible);
    Fixture();
    receiver.events[1].time = 1;
    CHECK(latency::Analyze(hooks, receiver).invalid != 0);
    return true;
}
static bool ReceiverSequences()
{
    Fixture();
    bool held[256] = {};
    loadedReceiver.header = receiver.header;
    loadedReceiver.header.count = loadedReceiver.header.sequenceErrors = 0;
    for (UINT i = 0; i < 4; ++i) latency::Record(loadedReceiver, receiver.events[i], held);
    CHECK(loadedReceiver.header.sequenceErrors == 0 && loadedReceiver.header.count == 4);
    latency::Record(loadedReceiver, receiver.events[3], held); // orphan UP
    CHECK(loadedReceiver.header.sequenceErrors == 1);
    latency::Record(loadedReceiver, receiver.events[0], held);
    latency::Record(loadedReceiver, receiver.events[0], held); // duplicate DOWN
    CHECK(loadedReceiver.header.sequenceErrors == 2);
    loadedReceiver.header.count = latency::kCapacity;
    latency::Record(loadedReceiver, receiver.events[0], held);
    CHECK(loadedReceiver.header.count == latency::kCapacity && loadedReceiver.header.dropped == 1);
    return true;
}
static bool CaptureIntegration()
{
    ZeroMemory(&latency::capture, sizeof(latency::capture));
    latency::Initialize(latency::capture.header, latency::kHookFile, "selftest", "capture");
    phase0::Reset();
    CHECK(phase0::Configure(&g_mappings[0], L"f6"));
    CHECK(phase0::Configure(&g_mappings[1], L"f7"));
    CHECK(phase0::Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(phase0::Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
    CHECK(phase0::Event(XBUTTON1, WM_XBUTTONUP) == 1);
    CHECK(phase0::Event(XBUTTON2, WM_XBUTTONUP) == 1);
    auto &c = latency::capture;
    CHECK(c.header.count == 4 && c.header.sends == 4);
    for (UINT i = 0; i < 4; ++i)
    {
        CHECK(c.sends[i].hook == i + 1 && c.sends[i].accepted == 1);
        CHECK(c.sends[i].keys[0].extra == 0 && c.hooks[i].begin <= c.sends[i].begin);
        CHECK(c.hooks[i].end >= c.sends[i].end && c.hooks[i].parent == 0);
    }
    c.header.count = c.header.sends = 0;
    phase0::s.accepted = 0;
    CHECK(phase0::Event(XBUTTON1, WM_XBUTTONDOWN) == phase0::kForwarded);
    CHECK(c.sends[0].accepted == 0);
    phase0::Event(XBUTTON1, WM_XBUTTONUP);
    c.header.count = c.header.sends = 0;
    phase0::s.accepted = phase0::kAcceptAll;
    phase0::s.onSend = []() { phase0::Event(XBUTTON2, WM_XBUTTONDOWN); };
    phase0::Event(XBUTTON1, WM_XBUTTONDOWN);
    CHECK(c.header.count == 2 && c.hooks[1].parent == 1 && c.sends[1].hook == 2 && latency::activeHook == 0);
    phase0::Event(XBUTTON1, WM_XBUTTONUP);
    phase0::Event(XBUTTON2, WM_XBUTTONUP);
    c.header.count = c.header.sends = latency::kCapacity;
    phase0::Event(XBUTTON1, WM_XBUTTONDOWN);
    CHECK(c.header.dropped == 1 && c.header.sendDropped == 1 && g_mappings[0].keyOwned);
    phase0::Event(XBUTTON1, WM_XBUTTONUP);
    CHECK(!g_mappings[0].keyOwned); // buffer exhaustion never changes production dispatch
    return true;
}
static bool FileRoundTrip()
{
    Fixture();
    // Unique self-test files are created only beside this executable in ignored build/latency/.
    sprintf_s(hooks.header.run, "selftest-%lu-%lld", GetCurrentProcessId(), latency::Now());
    strcpy_s(receiver.header.run, hooks.header.run);
    WCHAR hookPath[MAX_PATH], receiverPath[MAX_PATH];
    CHECK(latency::OutputPath(hookPath, hooks.header, L"hook"));
    CHECK(latency::OutputPath(receiverPath, receiver.header, L"receiver"));
    CHECK(latency::Save(hooks, hookPath));
    CHECK(!latency::Save(hooks, hookPath)); // refusal to overwrite
    CHECK(latency::Save(receiver, receiverPath));
    CHECK(latency::Load(loaded, hookPath) && latency::Load(loadedReceiver, receiverPath));
    CHECK(latency::Analyze(loaded, loadedReceiver).Clean(loaded.header, loadedReceiver.header));
    CHECK(DeleteFileW(hookPath));
    CHECK(DeleteFileW(receiverPath));
    HANDLE file = latency::NewOutput(hookPath);
    CHECK(file != INVALID_HANDLE_VALUE);
    CHECK(latency::WritePart(file, &hooks.header, sizeof(latency::Header))); // truncated payload
    CHECK(CloseHandle(file));
    CHECK(!latency::Load(loaded, hookPath));
    CHECK(DeleteFileW(hookPath));
    return true;
}
static bool Overhead()
{
    constexpr size_t n = 100000;
    static std::array<LONGLONG, n> samples;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    std::printf("capture_bytes=%zu receiver_bytes=%zu; compiler=%d; process_priority=%lu thread_priority=%d\n",
                sizeof(latency::HookTrace), sizeof(latency::ReceiverTrace), _MSC_FULL_VER,
                GetPriorityClass(GetCurrentProcess()), GetThreadPriority(GetCurrentThread()));
    using Callback = LRESULT(CALLBACK *)(int, WPARAM, LPARAM);
    Callback volatile call = HookBody;
    std::printf("SIMULATED OVERHEAD ONLY: count-only SendInput/next stubs, no OS input.\n"
                "10000 warmup + 100000 intervals/path/pass, 5 passes; paired DOWN+UP per interval.\n"
                "frequency=%lld; no outlier removal or percentile subtraction.\n", frequency.QuadPart);
    // Prefault all capture pages and sample storage before any timed interval.
    ZeroMemory(&latency::capture, sizeof(latency::capture));
    samples.fill(0);
    latency::Prefault(&latency::capture, sizeof(latency::capture));
    latency::Prefault(samples.data(), sizeof(samples));
    for (UINT pass = 0; pass < 5; ++pass) for (UINT offset = 0; offset < 4; ++offset)
    {
        const UINT mode = (offset + pass) % 4;
        phase0::Reset();
        CHECK(phase0::Configure(&g_mappings[0], L"f6"));
        CHECK(uninstrumented::ParseKeystroke(&uninstrumented::g_mappings[0], L"f6"));
        uninstrumented::PrepareMappings();
        phase0::s_record = false;
        call = mode == 3 ? MouseHookProc : uninstrumented::MouseHookProc;
        MSLLHOOKSTRUCT mouse = {};
        mouse.mouseData = static_cast<DWORD>(XBUTTON1) << 16;
        const auto sendsBefore = phase0::s_benchSends;
        for (size_t i = 0; i < n + 10000; ++i)
        {
            // Reuse the full preallocated buffer, resetting only when full, outside timing.
            if (latency::capture.header.count >= latency::kCapacity - 2)
                latency::capture.header.count = latency::capture.header.sends = 0;
            const LONGLONG begin = latency::Now();
            if (mode == 1)
            {
                // Same extra QPC count as two measured hooks plus two measured sends.
                for (UINT j = 0; j < 8; ++j) phase0::s_result = latency::Now();
            }
            if (mode >= 2)
            {
                call(HC_ACTION, WM_XBUTTONDOWN, reinterpret_cast<LPARAM>(&mouse));
                call(HC_ACTION, WM_XBUTTONUP, reinterpret_cast<LPARAM>(&mouse));
            }
            const LONGLONG end = latency::Now();
            if (i >= 10000) samples[i - 10000] = end - begin;
        }
        CHECK(phase0::s_benchSends - sendsBefore == (mode >= 2 ? 2 * (n + 10000) : 0));
        CHECK(latency::capture.header.sendDropped == 0 && latency::capture.header.dropped == 0);
        std::printf("pass=%u ", pass + 1);
        latency::Distribution(mode == 0 ? "empty_QPC" : mode == 1 ? "eight_extra_QPC_reads" :
                              mode == 2 ? "uninstrumented_pair" : "recorded_diagnostic_pair",
                              4, "DOWN+UP", std::vector<LONGLONG>(samples.begin(), samples.end()), frequency.QuadPart);
    }
    return true;
}
}
int main(int argc, char **argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--overhead")) return latencyTests::Overhead() ? 0 : 1;
    if (argc == 2 && !std::strcmp(argv[1], "--write-fixture"))
    {
        latencyTests::Fixture();
        WCHAR hp[MAX_PATH], rp[MAX_PATH];
        if (!latency::OutputPath(hp, latencyTests::hooks.header, L"hook") ||
            !latency::OutputPath(rp, latencyTests::receiver.header, L"receiver")) return 2;
        return latency::Save(latencyTests::hooks, hp) && latency::Save(latencyTests::receiver, rp) ? 0 : 1;
    }
    if (argc != 1) return 2;
    bool ok = phase0::RunTests(); // same 41 groups through the instrumented callback
    const struct { const char *name; bool (*run)(); } tests[] = {
        { "QPC correlation and receive-before-return", latencyTests::Correlation },
        { "signed zero/positive/one-tick/invalid timestamp policy", latencyTests::SignedTimestampPolicy },
        { "missing / extra / ambiguous / deadline", latencyTests::MissingUnexpectedAndAmbiguous },
        { "shared-key silent edges / complete legacy batch", latencyTests::SharedAndShortcut },
        { "source / scan / metadata / ordering / focus / overflow", latencyTests::ProvenanceAndOrdering },
        { "failed / partial / malformed / session mismatch", latencyTests::FailedAndMalformed },
        { "receiver DOWN/UP and repeat validation", latencyTests::ReceiverSequences },
        { "stubbed diagnostic capture / reentrancy / overflow", latencyTests::CaptureIntegration },
        { "trace roundtrip / truncation / overwrite refusal", latencyTests::FileRoundTrip },
    };
    UINT passed = 0;
    for (const auto &test : tests)
    {
        const bool result = test.run();
        std::printf("%s %s\n", result ? "PASS" : "FAIL", test.name);
        passed += result ? 1u : 0u;
        ok = result && ok;
    }
    std::printf("%u/%zu diagnostic groups; no real hook or desktop injection.\n", passed, ARRAYSIZE(tests));
    return ok ? 0 : 1;
}
