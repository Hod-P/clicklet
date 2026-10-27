// SPDX-License-Identifier: MIT
// Plain-key hold regression tests and simulated callback benchmarks.
// No real hook installation, tray operations or SendInput calls are reachable
// from the test/benchmark entry point. Do not call the included wWinMain.
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace phase0
{
UINT WINAPI SendStub(UINT count, LPINPUT inputs, int size);
LRESULT WINAPI NextStub(HHOOK hook, int code, WPARAM message, LPARAM data);
HHOOK WINAPI InstallStub(int kind, HOOKPROC callback, HINSTANCE instance, DWORD thread);
BOOL WINAPI UninstallStub(HHOOK hook);
BOOL WINAPI TrayStub(DWORD message, PNOTIFYICONDATAW data);
}

// Headers are already included, so these substitutions affect only Clicklet's
// call sites, not the Windows declarations. No production seam is introduced.
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

namespace phase0
{
constexpr LRESULT kForwarded = 73;
constexpr UINT kAcceptAll = static_cast<UINT>(-1);
struct Batch
{
    UINT count;
    int size;
    UINT accepted;
    INPUT inputs[KEYSTROKE_INPUT_MAX];
};
struct Stubs
{
    UINT sendCalls;
    UINT nextCalls;
    UINT installCalls;
    UINT uninstallCalls;
    UINT trayCalls;
    UINT accepted;
    BOOL installOK;
    BOOL uninstallOK;
    bool overflow;
    int forwardedCode;
    WPARAM forwardedMessage;
    LPARAM forwardedData;
    Batch batches[64];
    UINT returns[64];
    UINT returnCount;
    bool keyboard[256]; // accepted-prefix model, not a model of real Windows keyboard state
    void (*onSend)();
    LRESULT reentrantUp;
    LRESULT reentrantDown;
};
static Stubs s;
static bool s_record = true;
static volatile unsigned long long s_benchSends;
static volatile unsigned long long s_benchNext;
static volatile LRESULT s_result;

static HHOOK FakeHook()
{
    return reinterpret_cast<HHOOK>(static_cast<ULONG_PTR>(1));
}

__declspec(noinline) UINT WINAPI SendStub(UINT count, LPINPUT inputs, int size)
{
    if (!s_record)
    {
        ++s_benchSends;
        return s.accepted == kAcceptAll ? count : s.accepted;
    }
    const UINT index = s.sendCalls++;
    if (index >= ARRAYSIZE(s.batches) || count > KEYSTROKE_INPUT_MAX)
    {
        s.overflow = true;
        return 0;
    }
    Batch &batch = s.batches[index];
    batch.count = count;
    batch.size = size;
    const UINT result = index < s.returnCount ? s.returns[index] : s.accepted;
    batch.accepted = result == kAcceptAll ? count : result;
    for (UINT i = 0; i < count; ++i)
        batch.inputs[i] = inputs[i];
    for (UINT i = 0; i < batch.accepted && i < count; ++i)
        s.keyboard[inputs[i].ki.wVk] = (inputs[i].ki.dwFlags & KEYEVENTF_KEYUP) == 0;
    if (s.onSend != nullptr)
    {
        const auto callback = s.onSend;
        s.onSend = nullptr;
        callback();
    }
    return batch.accepted;
}

__declspec(noinline) LRESULT WINAPI NextStub(HHOOK, int code, WPARAM message, LPARAM data)
{
    if (!s_record)
        ++s_benchNext;
    else
    {
        ++s.nextCalls;
        s.forwardedCode = code;
        s.forwardedMessage = message;
        s.forwardedData = data;
    }
    return kForwarded;
}

HHOOK WINAPI InstallStub(int, HOOKPROC, HINSTANCE, DWORD)
{
    ++s.installCalls;
    return s.installOK ? FakeHook() : NULL;
}

BOOL WINAPI UninstallStub(HHOOK)
{
    ++s.uninstallCalls;
    return s.uninstallOK;
}

BOOL WINAPI TrayStub(DWORD, PNOTIFYICONDATAW)
{
    ++s.trayCalls;
    return TRUE;
}

static void Reset()
{
    s = {};
    s.accepted = kAcceptAll;
    s.installOK = TRUE;
    s.uninstallOK = TRUE;
    s_record = true;
    g_enabled = TRUE;
    g_hook = NULL;
    g_iniPath[0] = L'\0';
    ZeroMemory(g_keys, sizeof(g_keys));
    for (Mapping &mapping : g_mappings)
    {
        ZeroMemory(mapping.keystroke, sizeof(mapping.keystroke));
        mapping.keystrokeCount = 0;
        mapping.keystrokeText[0] = L'\0';
        mapping.downSwallowed = FALSE;
        mapping.downSeen = FALSE;
        mapping.pressSerial = 0;
        mapping.keyOwned = FALSE;
        mapping.key = NULL;
        mapping.pendingUps = 0;
        mapping.recovering = FALSE;
        ZeroMemory(mapping.recoveryMask, sizeof(mapping.recoveryMask));
    }
}

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #condition); \
    return false; } } while (false)

static bool Configure(Mapping *mapping, const WCHAR *text)
{
    if (!ParseKeystroke(mapping, text))
        return false;
    PrepareMappings();
    return true;
}

static LRESULT Event(WORD button, WPARAM message, DWORD flags = 0)
{
    MSLLHOOKSTRUCT mouse = {};
    mouse.mouseData = static_cast<DWORD>(button) << 16;
    mouse.flags = flags;
    return MouseHookProc(HC_ACTION, message, reinterpret_cast<LPARAM>(&mouse));
}

static bool Key(const Batch &batch, UINT index, WORD vk, DWORD flags)
{
    if (index >= batch.count)
        return false;
    const INPUT &input = batch.inputs[index];
    return input.type == INPUT_KEYBOARD && input.ki.wVk == vk &&
           input.ki.wScan == static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) &&
           input.ki.dwFlags == flags && input.ki.time == 0 && input.ki.dwExtraInfo == 0;
}

static bool ParserExamples()
{
    const struct { const WCHAR *text; WORD vk; DWORD flags; } examples[] =
    {
        { L"f6", VK_F6, 0 }, { L"F7", VK_F7, 0 },
        { L"f1", VK_F1, 0 }, { L"F24", VK_F24, 0 },
        { L"a", 'A', 0 }, { L"9", '9', 0 },
        { L"delete", VK_DELETE, KEYEVENTF_EXTENDEDKEY },
        { L"left", VK_LEFT, KEYEVENTF_EXTENDEDKEY },
        { L"enter", VK_RETURN, 0 },
    };
    for (const auto &example : examples)
    {
        Reset();
        CHECK(Configure(&g_mappings[0], example.text));
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        CHECK(s.sendCalls == 1 && s.batches[0].count == 1);
        CHECK(s.batches[0].size == static_cast<int>(sizeof(INPUT)));
        CHECK(Key(s.batches[0], 0, example.vk, example.flags));
        CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 2);
        CHECK(Key(s.batches[1], 0, example.vk, example.flags | KEYEVENTF_KEYUP));
    }
    return true;
}

static bool PlainTapAndRelease()
{
    for (WORD button : { static_cast<WORD>(XBUTTON1), static_cast<WORD>(XBUTTON2) })
    {
        Reset();
        const int index = button == XBUTTON1 ? 0 : 1;
        CHECK(Configure(&g_mappings[index], L"f6"));
        CHECK(Event(button, WM_XBUTTONDOWN) == 1);
        CHECK(s.sendCalls == 1 && g_mappings[index].downSwallowed);
        CHECK(s.keyboard[VK_F6] && g_mappings[index].keyOwned);
        CHECK(Event(button, WM_XBUTTONUP) == 1);
        CHECK(s.sendCalls == 2 && !g_mappings[index].downSwallowed && s.nextCalls == 0);
        CHECK(!s.keyboard[VK_F6] && !g_mappings[index].keyOwned);
    }
    return true;
}

static bool RepeatedDownDeduplicated()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(s.sendCalls == 1 && s.batches[0].count == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
    return true;
}

static bool UnmatchedUpAndFastTaps()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
    for (int i = 0; i < 8; ++i)
    {
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1);
    }
    CHECK(s.sendCalls == 16 && s.nextCalls == 1);
    return true;
}

static bool InterleavedButtons()
{
    for (int first : { 0, 1 })
    {
        Reset();
        CHECK(Configure(&g_mappings[0], L"f6"));
        CHECK(Configure(&g_mappings[1], L"f7"));
        const WORD held = first == 0 ? XBUTTON1 : XBUTTON2;
        const WORD tapped = first == 0 ? XBUTTON2 : XBUTTON1;
        CHECK(Event(held, WM_XBUTTONDOWN) == 1);
        for (int i = 0; i < 3; ++i)
        {
            CHECK(Event(tapped, WM_XBUTTONDOWN) == 1);
            CHECK(Event(tapped, WM_XBUTTONUP) == 1);
            CHECK(g_mappings[first].downSwallowed);
        }
        CHECK(Event(held, WM_XBUTTONUP) == 1);
        CHECK(s.sendCalls == 8 && s.nextCalls == 0);
        CHECK(Key(s.batches[0], 0, first == 0 ? VK_F6 : VK_F7, 0));
        CHECK(Key(s.batches[1], 0, first == 0 ? VK_F7 : VK_F6, 0));
    }
    return true;
}

static bool SameKeyOwnership()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Configure(&g_mappings[1], L"f6"));
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
    CHECK(s.sendCalls == 1 && s.keyboard[VK_F6] && g_mappings[0].key->owners == 2);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && g_mappings[1].downSwallowed);
    CHECK(s.sendCalls == 1 && s.keyboard[VK_F6]);
    CHECK(Event(XBUTTON2, WM_XBUTTONUP) == 1 && s.sendCalls == 2);
    CHECK(!s.keyboard[VK_F6] && Key(s.batches[1], 0, VK_F6, KEYEVENTF_KEYUP));
    return true;
}

static bool Forwarding()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == kForwarded);
    CHECK(Event(XBUTTON2, WM_XBUTTONUP) == kForwarded);
    CHECK(Event(3, WM_XBUTTONDOWN) == kForwarded);
    CHECK(MouseHookProc(-1, WM_XBUTTONDOWN, 0) == kForwarded);
    CHECK(s.forwardedCode == -1 && s.forwardedMessage == WM_XBUTTONDOWN && s.forwardedData == 0);
    CHECK(MouseHookProc(HC_ACTION, WM_MOUSEMOVE, 0) == kForwarded);
    CHECK(MouseHookProc(HC_ACTION, WM_LBUTTONDOWN, 0) == kForwarded);
    CHECK(s.sendCalls == 0 && s.nextCalls == 6);
    return true;
}

static bool InjectedFiltering()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN, LLMHF_INJECTED) == kForwarded);
    CHECK(!g_mappings[0].downSwallowed && s.sendCalls == 0);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP, LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED) == kForwarded);
    CHECK(g_mappings[0].downSwallowed);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 2);
    return true;
}

static bool DisabledGuardAndPendingUp()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    g_enabled = FALSE;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded && s.sendCalls == 0);
    g_enabled = TRUE;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    g_enabled = FALSE; // Exercise release even if enable state changes before UP.
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && !g_mappings[0].downSwallowed);
    return true;
}

static bool ShortcutOrdering()
{
    CHECK(Configure(&g_mappings[0], L"win+shift+alt+ctrl+delete"));
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.batches[0].count == 10);
    const WORD keys[] = { VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_DELETE,
                         VK_DELETE, VK_LWIN, VK_SHIFT, VK_MENU, VK_CONTROL };
    for (UINT i = 0; i < ARRAYSIZE(keys); ++i)
    {
        DWORD flags = (i >= 5) ? KEYEVENTF_KEYUP : 0;
        if (keys[i] == VK_LWIN || keys[i] == VK_DELETE)
            flags |= KEYEVENTF_EXTENDEDKEY;
        CHECK(Key(s.batches[0], i, keys[i], flags));
    }
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 1);
    return true;
}

static bool ZeroAndPartialSendResults()
{
    for (const WCHAR *text : { L"ctrl+enter", L"ctrl+alt+shift+win+delete" })
    {
        Reset();
        CHECK(Configure(&g_mappings[0], text));
        const UINT count = g_mappings[0].keystrokeCount;
        for (UINT accepted = 0; accepted <= count; ++accepted)
        {
            Reset();
            CHECK(Configure(&g_mappings[0], text));
            s.returns[0] = accepted;
            s.returnCount = 1;
            CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == (accepted == 0 ? kForwarded : 1));
            CHECK(g_mappings[0].downSwallowed == (accepted != 0));
            const Batch &batch = s.batches[0];
            CHECK(batch.count == count && batch.accepted == accepted && batch.size == static_cast<int>(sizeof(INPUT)));
            CHECK(Event(XBUTTON1, WM_XBUTTONUP) == (accepted == 0 ? kForwarded : 1));
            CHECK(!g_mappings[0].downSwallowed);
            CHECK(g_mappings[0].pendingUps == 0);
            for (bool held : s.keyboard)
                CHECK(!held);
        }
    }
    return true;
}

static bool FailureDoesNotCrossButtons()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Configure(&g_mappings[1], L"f7"));
    s.accepted = 0;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
    s.accepted = kAcceptAll;
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON2, WM_XBUTTONUP) == 1 && !g_mappings[0].downSwallowed);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
    CHECK(s.sendCalls == 3 && s.batches[0].accepted == 0 && s.batches[1].accepted == 1);
    return true;
}

static bool DisableAndFailedUnhook()
{
    for (BOOL success : { FALSE, TRUE })
    {
        Reset();
        CHECK(Configure(&g_mappings[0], L"f6"));
        g_hook = FakeHook();
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        s.uninstallOK = success;
        SetEnabled(FALSE);
        CHECK(!g_enabled && g_mappings[0].downSwallowed == !success);
        CHECK(g_hook == (success ? NULL : FakeHook()));
        CHECK(s.uninstallCalls == 1 && s.trayCalls == 1 && s.sendCalls == 2);
        CHECK(!s.keyboard[VK_F6] && !g_mappings[0].keyOwned);
        // Directly simulate delivery even when the real hook would be absent.
        CHECK(Event(XBUTTON1, WM_XBUTTONUP) == (success ? kForwarded : 1));
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded && s.sendCalls == 2);
        SetEnabled(TRUE);
        CHECK(g_enabled && g_hook == FakeHook());
        CHECK(s.installCalls == (success ? 1u : 0u));
    }
    return true;
}

static bool EnableFailure()
{
    g_enabled = FALSE;
    s.installOK = FALSE;
    SetEnabled(TRUE);
    CHECK(!g_enabled && g_hook == NULL && s.installCalls == 1 && s.trayCalls == 1);
    CHECK(s.uninstallCalls == 0);
    return true;
}

static bool ShutdownReleasesPlainKey()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Configure(&g_mappings[1], L"ctrl+enter"));
    g_hook = FakeHook();
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
    UninstallHook(); // Exercise the exact cleanup helper used by wWinMain on exit.
    CHECK(!g_mappings[0].downSwallowed && !g_mappings[1].downSwallowed);
    CHECK(g_hook == NULL && s.uninstallCalls == 1 && s.sendCalls == 3);
    CHECK(!s.keyboard[VK_F6]);
    return true;
}

static bool FailedPlainDownIsPaired()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    s.returns[0] = 0;
    s.returnCount = 1;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
    CHECK(!g_mappings[0].keyOwned && !g_mappings[0].key->held);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded && s.sendCalls == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded && s.sendCalls == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 3);
    return true;
}

static bool ReleaseRetryAndPendingRecovery()
{
    for (bool retrySucceeds : { false, true })
    {
        Reset();
        CHECK(Configure(&g_mappings[0], L"f6"));
        s.returns[0] = 1;
        s.returns[1] = 0;
        s.returns[2] = retrySucceeds ? 1u : 0u;
        s.returnCount = 3;
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 3);
        CHECK(g_mappings[0].key->held == !retrySucceeds);
        CHECK(s.keyboard[VK_F6] == !retrySucceeds && g_mappings[0].key->owners == 0);
        CHECK(!g_mappings[0].keyOwned);
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        CHECK(s.sendCalls == (retrySucceeds ? 4u : 5u));
        CHECK(g_mappings[0].key->owners == 1 && s.keyboard[VK_F6]);
        CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && !s.keyboard[VK_F6]);
    }
    return true;
}

static bool PendingReleaseRefusesNewPress()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    s.returns[0] = 1;
    for (UINT i = 1; i < 5; ++i)
        s.returns[i] = 0;
    s.returnCount = 5;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 3);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded && s.sendCalls == 5);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded && s.sendCalls == 5);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded && s.sendCalls == 6);
    CHECK(!s.keyboard[VK_F6] && !g_mappings[0].key->held);
    return true;
}

static bool SharedKeyAllOrdersAndAliases()
{
    for (int firstDown : { 0, 1 })
        for (int firstUp : { 0, 1 })
        {
            Reset();
            CHECK(Configure(&g_mappings[0], L"del"));
            CHECK(Configure(&g_mappings[1], L"delete"));
            CHECK(g_mappings[0].key == g_mappings[1].key);
            const WORD downButton = firstDown == 0 ? XBUTTON1 : XBUTTON2;
            const WORD otherDown = firstDown == 0 ? XBUTTON2 : XBUTTON1;
            const WORD upButton = firstUp == 0 ? XBUTTON1 : XBUTTON2;
            const WORD otherUp = firstUp == 0 ? XBUTTON2 : XBUTTON1;
            CHECK(Event(downButton, WM_XBUTTONDOWN) == 1);
            CHECK(Event(otherDown, WM_XBUTTONDOWN) == 1);
            CHECK(Event(otherDown, WM_XBUTTONDOWN) == 1 && s.sendCalls == 1);
            CHECK(Event(upButton, WM_XBUTTONUP) == 1 && s.sendCalls == 1);
            CHECK(s.keyboard[VK_DELETE] && g_mappings[0].key->owners == 1);
            CHECK(Event(otherUp, WM_XBUTTONUP) == 1 && s.sendCalls == 2);
            CHECK(!s.keyboard[VK_DELETE] && g_mappings[0].key->owners == 0);
        }
    return true;
}

static bool SharedKeyDisableAndReenable()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Configure(&g_mappings[1], L"f6"));
    g_hook = FakeHook();
    s.uninstallOK = FALSE;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
    SetEnabled(FALSE);
    CHECK(s.sendCalls == 2 && !s.keyboard[VK_F6]);
    CHECK(!g_mappings[0].keyOwned && !g_mappings[1].keyOwned);
    SetEnabled(TRUE);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.sendCalls == 2);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.sendCalls == 3);
    // The old second button's UP must not decrement the new first owner's count.
    CHECK(Event(XBUTTON2, WM_XBUTTONUP) == 1 && s.sendCalls == 3);
    CHECK(g_mappings[0].key->owners == 1 && s.keyboard[VK_F6]);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 4);
    return true;
}

static bool LifecycleReleaseFailure()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    g_hook = FakeHook();
    s.returns[0] = 1;
    s.returns[1] = 0;
    s.returns[2] = 0;
    s.returnCount = 3;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    SetEnabled(FALSE);
    CHECK(g_mappings[0].key->held && s.keyboard[VK_F6] && s.sendCalls == 3);
    CHECK(g_mappings[0].key->owners == 0 && !g_mappings[0].keyOwned);
    SetEnabled(TRUE);
    CHECK(!g_mappings[0].key->held && !s.keyboard[VK_F6] && s.sendCalls == 4);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
    return true;
}

static bool ShutdownBothKeysAndSharedKey()
{
    for (const WCHAR *second : { L"f6", L"f7" })
    {
        Reset();
        CHECK(Configure(&g_mappings[0], L"f6"));
        CHECK(Configure(&g_mappings[1], second));
        g_hook = FakeHook();
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
        UninstallHook();
        CHECK(!s.keyboard[VK_F6] && !s.keyboard[VK_F7]);
        CHECK(s.sendCalls == (second[1] == L'6' ? 2u : 4u));
    }
    return true;
}

static bool ShortcutRepeatAndRecoveryFailure()
{
    CHECK(Configure(&g_mappings[0], L"ctrl+enter"));
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.sendCalls == 2);
    s.returns[2] = 0;
    s.returnCount = 3;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.sendCalls == 3);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1);
    Reset();
    CHECK(Configure(&g_mappings[0], L"ctrl+enter"));
    s.returns[0] = 1;
    s.returns[1] = 0;
    s.returns[2] = 0;
    s.returnCount = 3;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.sendCalls == 3);
    CHECK(s.keyboard[VK_CONTROL] && g_mappings[0].pendingUps != 0);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 4);
    CHECK(!s.keyboard[VK_CONTROL] && g_mappings[0].pendingUps == 0);
    return true;
}

static void ReenterDown() { s_result = Event(XBUTTON1, WM_XBUTTONDOWN); }
static void ReenterUp() { s_result = Event(XBUTTON1, WM_XBUTTONUP); }
static void ReenterDisable() { SetEnabled(FALSE); }
static void ReenterUpThenDown()
{
    s_result = Event(XBUTTON1, WM_XBUTTONUP);
    s_result = Event(XBUTTON1, WM_XBUTTONDOWN);
}

static bool ReentrantTransitions()
{
    for (auto callback : { ReenterDown, ReenterUp, ReenterDisable })
    {
        Reset();
        CHECK(Configure(&g_mappings[0], L"f6"));
        g_hook = FakeHook();
        s.onSend = callback;
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        if (callback == ReenterDown)
        {
            CHECK(s_result == 1 && s.sendCalls == 1);
            CHECK(g_mappings[0].key->owners == 1);
            CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 2);
        }
        else
            CHECK(s.sendCalls == 2 && g_mappings[0].key->owners == 0);
        CHECK(!s.keyboard[VK_F6]);
    }
    return true;
}

static bool AllRecognizedPlainKeys()
{
    for (const auto &key : kNamedKeys)
    {
        Reset();
        CHECK(Configure(&g_mappings[0], key.name));
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.keyboard[key.vk]);
        CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && !s.keyboard[key.vk]);
    }
    for (int number = 1; number <= 24; ++number)
    {
        Reset();
        WCHAR name[8];
        CHECK(SUCCEEDED(StringCchPrintfW(name, ARRAYSIZE(name), L"f%d", number)));
        CHECK(Configure(&g_mappings[0], name));
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 2);
    }
    for (WCHAR name[2] = { L'0', 0 }; name[0] <= L'Z'; ++name[0])
    {
        if (name[0] > L'9' && name[0] < L'A')
            continue;
        Reset();
        CHECK(Configure(&g_mappings[0], name));
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.keyboard[name[0]]);
        CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && !s.keyboard[name[0]]);
    }
    return true;
}

static bool ExhaustivePlainSequences()
{
    for (bool shared : { false, true })
        for (UINT sequence = 0; sequence < 4096; ++sequence)
        {
            Reset();
            CHECK(Configure(&g_mappings[0], L"f6"));
            CHECK(Configure(&g_mappings[1], shared ? L"f6" : L"f7"));
            UINT pressed = 0;
            UINT remaining = sequence;
            for (int step = 0; step < 6; ++step)
            {
                const UINT action = remaining & 3u;
                remaining >>= 2;
                const UINT button = action / 2;
                const UINT bit = 1u << button;
                const bool down = (action & 1u) == 0;
                const bool swallowed = down || (pressed & bit) != 0;
                pressed = down ? pressed | bit : pressed & ~bit;
                CHECK(Event(button == 0 ? XBUTTON1 : XBUTTON2, down ? WM_XBUTTONDOWN : WM_XBUTTONUP) ==
                      (swallowed ? 1 : kForwarded));
                const UINT first = (pressed & 1u) != 0 ? 1u : 0u;
                const UINT second = (pressed & 2u) != 0 ? 1u : 0u;
                CHECK(g_mappings[0].key->owners == (shared ? first + second : first));
                CHECK(g_mappings[1].key->owners == (shared ? first + second : second));
                CHECK(s.keyboard[VK_F6] == (shared ? pressed != 0 : first != 0));
                CHECK(s.keyboard[VK_F7] == (!shared && second != 0));
                CHECK(g_mappings[0].keyOwned == (first != 0) && g_mappings[1].keyOwned == (second != 0));
            }
            UninstallHook();
            CHECK(!s.keyboard[VK_F6] && !s.keyboard[VK_F7] && !s.overflow);
        }
    return true;
}

static bool ReentrantCleanupUp()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Configure(&g_mappings[1], L"f7"));
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
    s.onSend = ReenterUp;
    UninstallHook();
    CHECK(s_result == 1 && s.sendCalls == 4);
    CHECK(g_keys[0].owners == 0 && g_keys[1].owners == 0);
    CHECK(!s.keyboard[VK_F6] && !s.keyboard[VK_F7]);
    return true;
}

static bool FailedPressDoesNotOwnSharedKey()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Configure(&g_mappings[1], L"f6"));
    s.returns[0] = 0;
    s.returnCount = 1;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1 && s.sendCalls == 2);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded && s.sendCalls == 2);
    CHECK(g_mappings[1].key->owners == 1 && s.keyboard[VK_F6]);
    CHECK(Event(XBUTTON2, WM_XBUTTONUP) == 1 && s.sendCalls == 3 && !s.keyboard[VK_F6]);
    return true;
}

static bool ShutdownRefusalRemainsExplicit()
{
    for (bool shortcut : { false, true })
    {
        Reset();
        CHECK(Configure(&g_mappings[0], shortcut ? L"ctrl+enter" : L"f6"));
        g_hook = FakeHook();
        s.accepted = 0; // All recovery calls are refused indefinitely.
        s.returns[0] = 1;
        s.returnCount = 1;
        CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
        UninstallHook();
        CHECK(g_hook == NULL && !g_enabled && !g_mappings[0].keyOwned);
        if (shortcut)
            CHECK(s.sendCalls == 5 && s.keyboard[VK_CONTROL] && g_mappings[0].pendingUps != 0);
        else
            CHECK(s.sendCalls == 3 && s.keyboard[VK_F6] && g_mappings[0].key->held);
    }
    return true;
}

static bool ReentrantPressReplacement()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    s.onSend = ReenterUpThenDown;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(s_result == kForwarded && s.sendCalls == 2);
    CHECK(!s.keyboard[VK_F6] && !g_mappings[0].keyOwned);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
    return true;
}

static bool ShortcutDisablePairing()
{
    CHECK(Configure(&g_mappings[0], L"ctrl+enter"));
    g_hook = FakeHook();
    s.uninstallOK = FALSE;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    SetEnabled(FALSE);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.sendCalls == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
    Reset();
    CHECK(Configure(&g_mappings[0], L"ctrl+enter"));
    g_hook = FakeHook();
    s.onSend = ReenterDisable;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(!g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
    SetEnabled(TRUE);
    s.accepted = 0;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
    return true;
}

static void CaptureUp() { s.reentrantUp = Event(XBUTTON1, WM_XBUTTONUP); }
static void CaptureUpThenDown()
{
    CaptureUp();
    s.reentrantDown = Event(XBUTTON1, WM_XBUTTONDOWN);
}
static void DisableWithHookRetained()
{
    g_hook = FakeHook();
    s.uninstallOK = FALSE;
    SetEnabled(FALSE);
}
static void DisableThenUp()
{
    DisableWithHookRetained();
    CaptureUp();
}

static bool RejectedPlainDownAfterReentrantUp()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    s.accepted = 0;
    s.onSend = CaptureUp;
    // UP was consumed while DOWN injection was unresolved: consume DOWN too.
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(s.reentrantUp == 1 && s.nextCalls == 0 && s.sendCalls == 1);
    CHECK(!g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
    CHECK(!g_mappings[0].keyOwned && !g_mappings[0].key->held);
    return true;
}

static bool PlainDispatchReentrantResults()
{
    for (UINT accepted : { 0u, 1u })
        for (auto callback : { CaptureUp, CaptureUpThenDown, DisableWithHookRetained, DisableThenUp })
        {
            Reset();
            CHECK(Configure(&g_mappings[0], L"f6"));
            s.returns[0] = accepted;
            s.returnCount = 1;
            s.onSend = callback;
            const bool ended = callback != DisableWithHookRetained;
            CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == (accepted != 0 || ended ? 1 : kForwarded));
            if (ended)
                CHECK(s.reentrantUp == 1);
            CHECK(s.sendCalls == (accepted != 0 ? 2u : 1u));
            CHECK(!s.keyboard[VK_F6] && !g_mappings[0].keyOwned && g_mappings[0].key->owners == 0);
            if (callback == CaptureUpThenDown)
            {
                CHECK(s.reentrantDown == kForwarded && g_mappings[0].pressSerial == 2);
                CHECK(g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
                CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
            }
            if (callback == DisableWithHookRetained)
                CHECK(Event(XBUTTON1, WM_XBUTTONUP) == (accepted != 0 ? 1 : kForwarded));
            CHECK(!g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
        }
    return true;
}

// Create pending state through refused releases, not by writing production state.
static bool SeedPendingRelease(bool shortcut)
{
    CHECK(Configure(&g_mappings[0], shortcut ? L"ctrl+enter" : L"f6"));
    s.accepted = 0;
    s.returns[0] = 1;
    s.returnCount = 1;
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1);
    CHECK(shortcut ? g_mappings[0].pendingUps != 0 : g_mappings[0].key->held);
    return true;
}

static bool PlainRecoveryRevalidatesPress()
{
    for (UINT recovered : { 0u, 1u })
        for (auto callback : { CaptureUp, CaptureUpThenDown, DisableWithHookRetained, DisableThenUp })
        {
            Reset();
            CHECK(SeedPendingRelease(false));
            const UINT before = s.sendCalls;
            s.accepted = recovered;
            s.onSend = callback;
            CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
            CHECK(!g_mappings[0].keyOwned && g_mappings[0].key->owners == 0);
            CHECK(s.sendCalls == before + (recovered != 0 ? 1u : 2u));
            for (UINT i = before; i < s.sendCalls; ++i)
                CHECK(Key(s.batches[i], 0, VK_F6, KEYEVENTF_KEYUP));
            CHECK(g_mappings[0].key->held == (recovered == 0));
            if (callback != DisableWithHookRetained)
                CHECK(s.reentrantUp == kForwarded);
            if (callback == CaptureUpThenDown)
            {
                CHECK(s.reentrantDown == kForwarded && g_mappings[0].pressSerial == 3);
                CHECK(g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
                CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
                CHECK(s.sendCalls == before + (recovered != 0 ? 1u : 2u));
            }
            if (callback == DisableWithHookRetained || callback == DisableThenUp)
                CHECK(!g_enabled);
        }
    return true;
}

static bool ShortcutRecoveryRevalidatesPress()
{
    for (UINT recovered : { 0u, 1u })
        for (auto callback : { CaptureUp, CaptureUpThenDown, DisableWithHookRetained, DisableThenUp })
        {
            Reset();
            CHECK(SeedPendingRelease(true));
            const UINT before = s.sendCalls;
            s.accepted = recovered;
            s.onSend = callback;
            CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
            CHECK(s.sendCalls == before + (recovered != 0 ? 1u : 2u));
            for (UINT i = before; i < s.sendCalls; ++i)
                CHECK(Key(s.batches[i], 0, VK_CONTROL, KEYEVENTF_KEYUP));
            CHECK((g_mappings[0].pendingUps != 0) == (recovered == 0));
            if (callback != DisableWithHookRetained)
                CHECK(s.reentrantUp == kForwarded);
            if (callback == CaptureUpThenDown)
            {
                CHECK(s.reentrantDown == kForwarded && g_mappings[0].pressSerial == 3);
                CHECK(g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
            }
            if (callback == DisableWithHookRetained || callback == DisableThenUp)
                CHECK(!g_enabled);
        }
    return true;
}

static bool ShortcutDispatchReservesSuppression()
{
    for (const WCHAR *text : { L"ctrl+enter", L"ctrl+alt+shift+win+delete" })
    {
        CHECK(Configure(&g_mappings[0], text));
        const UINT count = g_mappings[0].keystrokeCount;
        for (UINT resultIndex = 0; resultIndex <= count; ++resultIndex)
            for (bool repeat : { false, true })
            for (auto callback : { CaptureUp, CaptureUpThenDown, DisableWithHookRetained, DisableThenUp })
            {
                const UINT accepted = count - resultIndex;
                Reset();
                CHECK(Configure(&g_mappings[0], text));
                if (repeat)
                    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
                const UINT before = s.sendCalls;
                s.returns[before] = accepted;
                s.returnCount = before + 1;
                s.onSend = callback;
                const bool ended = callback != DisableWithHookRetained;
                CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == (accepted != 0 || ended || repeat ? 1 : kForwarded));
                if (ended)
                    CHECK(s.reentrantUp == 1);
                if (callback == CaptureUpThenDown)
                {
                    CHECK(s.reentrantDown == kForwarded && g_mappings[0].pressSerial == 2);
                    CHECK(g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
                    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
                    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
                }
                if (callback == DisableWithHookRetained)
                    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == (accepted != 0 || repeat ? 1 : kForwarded));
                CHECK(!g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
                CHECK(g_mappings[0].pendingUps == 0 && !g_mappings[0].recovering);
                for (bool held : s.keyboard)
                    CHECK(!held);
            }
    }
    return true;
}

static void ArmCleanupReplacement() { s.onSend = CaptureUpThenDown; }
static void ArmCleanupDisable() { s.onSend = DisableThenUp; }

static bool ShortcutPartialCleanupReentrancy()
{
    for (UINT accepted = 1; accepted < 4; ++accepted)
        for (UINT released : { 0u, 1u })
            for (auto callback : { ArmCleanupReplacement, ArmCleanupDisable })
            {
                Reset();
                CHECK(Configure(&g_mappings[0], L"ctrl+enter"));
                s.returns[0] = accepted;
                s.returns[1] = released;
                s.returnCount = 2;
                s.onSend = callback;
                CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
                CHECK(s.reentrantUp == 1);
                if (callback == ArmCleanupReplacement)
                {
                    CHECK(s.reentrantDown == kForwarded);
                    CHECK(g_mappings[0].downSeen && !g_mappings[0].downSwallowed);
                    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == kForwarded);
                }
                else
                    CHECK(!g_enabled && !g_mappings[0].downSeen);
                CHECK(g_mappings[0].pendingUps == 0 && !g_mappings[0].recovering);
                for (UINT i = 1; i < s.sendCalls; ++i)
                    CHECK(s.batches[i].count == 1 && (s.batches[i].inputs[0].ki.dwFlags & KEYEVENTF_KEYUP) != 0);
                for (bool held : s.keyboard)
                    CHECK(!held);
            }
    return true;
}

static bool ShortcutReentrantDuplicateAndFailedRepeat()
{
    for (UINT accepted = 0; accepted <= 4; ++accepted)
        for (bool repeat : { false, true })
        {
            Reset();
            CHECK(Configure(&g_mappings[0], L"ctrl+enter"));
            if (repeat)
                CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
            const UINT before = s.sendCalls;
            s.returns[before] = accepted;
            s.returnCount = before + 1;
            s.onSend = ReenterDown;
            CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == (accepted != 0 || repeat ? 1 : kForwarded));
            CHECK(s_result == 1);
            CHECK(g_mappings[0].downSwallowed == (accepted != 0 || repeat));
            const UINT recoveryCalls = accepted == 0 || accepted == 4 ? 0u : (accepted == 2 ? 2u : 1u);
            CHECK(s.sendCalls == before + 1 + recoveryCalls);
            CHECK(Event(XBUTTON1, WM_XBUTTONUP) == (accepted != 0 || repeat ? 1 : kForwarded));
            CHECK(g_mappings[0].pendingUps == 0);
        }
    return true;
}

static bool PlainHoldShortcutCollisionPolicy()
{
    CHECK(Configure(&g_mappings[0], L"f6"));
    CHECK(Configure(&g_mappings[1], L"ctrl+f6"));
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.keyboard[VK_F6]);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1 && s.batches[1].count == 4);
    // Legacy shortcut UP interrupts the effective hold; plain ownership remains.
    CHECK(!s.keyboard[VK_F6] && g_mappings[0].keyOwned && g_mappings[0].key->owners == 1);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1 && s.sendCalls == 2);
    CHECK(Event(XBUTTON2, WM_XBUTTONUP) == 1 && s.sendCalls == 2);
    CHECK(Event(XBUTTON1, WM_XBUTTONUP) == 1 && s.sendCalls == 3);
    CHECK(Key(s.batches[2], 0, VK_F6, KEYEVENTF_KEYUP));
    CHECK(!g_mappings[0].key->held && !g_mappings[0].keyOwned);
    return true;
}

static bool FixturePath(const WCHAR *relative)
{
    const DWORD length = GetModuleFileNameW(NULL, g_iniPath, ARRAYSIZE(g_iniPath));
    CHECK(length > 0 && length < ARRAYSIZE(g_iniPath));
    WCHAR *slash = std::wcsrchr(g_iniPath, L'\\');
    CHECK(slash != nullptr);
    slash[1] = L'\0';
    CHECK(SUCCEEDED(StringCchCatW(g_iniPath, ARRAYSIZE(g_iniPath), relative)));
    return true;
}

static bool DefaultConfiguration()
{
    CHECK(FixturePath(L"phase0-intentionally-absent.ini"));
    CHECK(GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    WCHAR problem[256] = {};
    CHECK(LoadConfiguration(problem, ARRAYSIZE(problem)));
    PrepareMappings();
    CHECK(g_mappings[0].keystrokeCount == 0 && g_mappings[1].keystrokeCount == 4);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == kForwarded);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
    CHECK(Key(s.batches[0], 0, VK_CONTROL, 0));
    CHECK(Key(s.batches[0], 1, VK_RETURN, 0));
    CHECK(Key(s.batches[0], 2, VK_RETURN, KEYEVENTF_KEYUP));
    CHECK(Key(s.batches[0], 3, VK_CONTROL, KEYEVENTF_KEYUP));
    return true;
}

static bool PresentConfiguration()
{
    CHECK(FixturePath(L"fixtures\\one.ini"));
    CHECK(GetFileAttributesW(g_iniPath) != INVALID_FILE_ATTRIBUTES);
    WCHAR problem[256] = {};
    CHECK(LoadConfiguration(problem, ARRAYSIZE(problem)));
    PrepareMappings();
    CHECK(g_mappings[0].keystrokeCount == 2 && g_mappings[1].keystrokeCount == 0);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == kForwarded);
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Key(s.batches[0], 0, VK_DELETE, KEYEVENTF_EXTENDEDKEY));
    Reset();
    CHECK(FixturePath(L"fixtures\\both.ini"));
    CHECK(LoadConfiguration(problem, ARRAYSIZE(problem)));
    PrepareMappings();
    CHECK(Event(XBUTTON1, WM_XBUTTONDOWN) == 1);
    CHECK(Event(XBUTTON2, WM_XBUTTONDOWN) == 1);
    CHECK(Key(s.batches[0], 0, VK_F6, 0) && Key(s.batches[1], 0, VK_F7, 0));
    return true;
}

static bool RunTests()
{
    const struct { const char *name; bool (*run)(); } tests[] =
    {
        { "parser examples / precomputed INPUTs", ParserExamples },
        { "plain DOWN / hold / physical UP", PlainTapAndRelease },
        { "repeated plain DOWN is deduplicated", RepeatedDownDeduplicated },
        { "unmatched UP / fast taps", UnmatchedUpAndFastTaps },
        { "interleaved buttons both orders", InterleavedButtons },
        { "same key shares one DOWN and final UP", SameKeyOwnership },
        { "unmapped / unknown / negative code / non-XBUTTON forwarding", Forwarding },
        { "injected-event filtering preserves pending UP", InjectedFiltering },
        { "disabled DOWN guard / pending UP branch", DisabledGuardAndPendingUp },
        { "legacy modifier order / extended keys", ShortcutOrdering },
        { "all zero / partial / full shortcut SendInput results (4, 10 INPUTs)", ZeroAndPartialSendResults },
        { "failure / success interleaving", FailureDoesNotCrossButtons },
        { "disable / failed unhook / re-enable", DisableAndFailedUnhook },
        { "failed hook install leaves disabled", EnableFailure },
        { "shutdown cleanup releases plain key", ShutdownReleasesPlainKey },
        { "failed plain DOWN preserves pass-through until UP", FailedPlainDownIsPaired },
        { "UP retry / retained pending release / next press", ReleaseRetryAndPendingRecovery },
        { "pending release refusal passes new press", PendingReleaseRefusesNewPress },
        { "shared key all press/release orders and aliases", SharedKeyAllOrdersAndAliases },
        { "shared key disable/re-enable ownership separation", SharedKeyDisableAndReenable },
        { "disable release refusal / re-enable recovery", LifecycleReleaseFailure },
        { "shutdown releases independent and shared holds", ShutdownBothKeysAndSharedKey },
        { "legacy repeats / failed repeat / partial recovery retry", ShortcutRepeatAndRecoveryFailure },
        { "reentrant DOWN / UP / disable", ReentrantTransitions },
        { "all supported plain names, F1-F24, letters/digits", AllRecognizedPlainKeys },
        { "8192 exhaustive six-event sequences (distinct/shared keys)", ExhaustivePlainSequences },
        { "reentrant physical UP during shutdown cleanup", ReentrantCleanupUp },
        { "failed press never owns the other button's shared key", FailedPressDoesNotOwnSharedKey },
        { "shutdown refusal is bounded and retains unresolved state", ShutdownRefusalRemainsExplicit },
        { "reentrant UP/new DOWN cannot acquire stale ownership", ReentrantPressReplacement },
        { "shortcut disable pairing / reentrant disable flags", ShortcutDisablePairing },
        { "rejected plain DOWN after consumed reentrant UP", RejectedPlainDownAfterReentrantUp },
        { "plain zero/full result with reentrant UP / replacement / disable", PlainDispatchReentrantResults },
        { "plain pending recovery revalidates enable / press / serial", PlainRecoveryRevalidatesPress },
        { "shortcut pending recovery revalidates enable / press / serial", ShortcutRecoveryRevalidatesPress },
        { "shortcut zero/partial/full reentrant suppression reservation", ShortcutDispatchReservesSuppression },
        { "partial shortcut cleanup reentrant replacement / disable", ShortcutPartialCleanupReentrancy },
        { "shortcut reentrant duplicate / zero-result repeat rollback", ShortcutReentrantDuplicateAndFailedRepeat },
        { "plain hold versus legacy modifier shortcut collision policy", PlainHoldShortcutCollisionPolicy },
        { "absent test INI uses Ctrl+Enter default", DefaultConfiguration },
        { "present test INI replaces default", PresentConfiguration },
    };
    UINT passed = 0;
    for (const auto &test : tests)
    {
        Reset();
        if (!test.run() || s.overflow)
        {
            std::fprintf(stderr, "FAILED: %s (trace overflow=%d)\n", test.name, s.overflow);
            continue;
        }
        ++passed;
        std::printf("PASS %s\n", test.name);
    }
    std::printf("%u/%zu regression groups PASS. Stubbed input only.\n",
                passed, ARRAYSIZE(tests));
    return passed == ARRAYSIZE(tests);
}

// One callback per sample, with fixed count-only stubs. No OS hook or SendInput.
// A volatile pointer prevents devirtualizing/inlining the measured boundary.
static HOOKPROC volatile s_timedCallback = MouseHookProc;
__declspec(noinline) static LRESULT CALLBACK EmptyCallback(int, WPARAM, LPARAM)
{
    return 0;
}

// Count-only benchmark: prepare a fresh hypothetical event, never a real key.
// In batch mode these extra state stores are included in the measured interval.
static void PrepareSample(bool pending)
{
    Mapping &mapping = g_mappings[0];
    mapping.downSeen = pending ? TRUE : FALSE;
    mapping.downSwallowed = pending ? TRUE : FALSE;
    mapping.keyOwned = pending && mapping.key != NULL;
    mapping.pendingUps = 0;
    mapping.recovering = FALSE;
    if (mapping.key != NULL)
    {
        mapping.key->owners = pending ? 1u : 0u;
        mapping.key->held = pending ? TRUE : FALSE;
        mapping.key->sending = FALSE;
    }
}

static bool Benchmark(bool batched)
{
    constexpr size_t kSamples = 100000;
    constexpr size_t kWarmup = 10000;
    constexpr int kRuns = 5;
    const size_t batchSize = batched ? 64 : 1;
    static std::array<LONGLONG, kSamples> ticks;
    s_benchSends = 0;
    s_benchNext = 0;
    LARGE_INTEGER frequency;
    CHECK(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0);
    std::printf("SIMULATED callback only; QPC frequency=%lld Hz; MSVC=%d; pointer=%zu bytes\n",
                frequency.QuadPart, _MSC_FULL_VER, sizeof(void *));
    std::printf("Process priority class=%lu; thread priority=%d\n",
                GetPriorityClass(GetCurrentProcess()), GetThreadPriority(GetCurrentThread()));
    std::printf("10000 warmup + 100000 samples/path/run; 5 runs; count-only noinline stubs; batch=%zu\n", batchSize);
    std::printf("%s\n", batched ?
                "BATCH MEANS: includes per-call state preparation and loop; not individual callback percentiles" :
                "INDIVIDUAL intervals: includes timer/harness overhead; no overhead subtraction");
    std::printf("run,path,p50_ns_per_call,p95_ns_per_call,p99_ns_per_call,worst_ns_per_call\n");
    const struct
    {
        const char *name;
        WPARAM message;
        WORD button;
        DWORD flags;
        bool pending;
        const WCHAR *mapping;
        UINT accepted;
    } paths[] =
    {
        { "qpc_bracket", 0, 0, 0, false, L"f6", kAcceptAll },
        { "empty_callback", 0, 0, 0, false, L"f6", kAcceptAll },
        { "mousemove_forward", WM_MOUSEMOVE, 0, 0, false, L"f6", kAcceptAll },
        { "unmapped_down", WM_XBUTTONDOWN, XBUTTON2, 0, false, L"f6", kAcceptAll },
        { "injected_down", WM_XBUTTONDOWN, XBUTTON1, LLMHF_INJECTED, false, L"f6", kAcceptAll },
        { "plain_down_1_input", WM_XBUTTONDOWN, XBUTTON1, 0, false, L"f6", kAcceptAll },
        { "paired_up", WM_XBUTTONUP, XBUTTON1, 0, true, L"f6", kAcceptAll },
        { "unmatched_up", WM_XBUTTONUP, XBUTTON1, 0, false, L"f6", kAcceptAll },
        { "repeated_down", WM_XBUTTONDOWN, XBUTTON1, 0, true, L"f6", kAcceptAll },
        { "shortcut_down_4_inputs", WM_XBUTTONDOWN, XBUTTON1, 0, false, L"ctrl+enter", kAcceptAll },
        { "zero_send_down", WM_XBUTTONDOWN, XBUTTON1, 0, false, L"f6", 0 },
        { "partial_send_down", WM_XBUTTONDOWN, XBUTTON1, 0, false, L"ctrl+enter", 1 },
    };
    for (int run = 1; run <= kRuns; ++run)
    {
        // Rotate path order between runs to reduce a fixed order's load bias.
        for (size_t offset = 0; offset < ARRAYSIZE(paths); ++offset)
        {
            const size_t pathIndex = (offset + static_cast<size_t>(run - 1)) % ARRAYSIZE(paths);
            const auto &path = paths[pathIndex];
            Reset();
            CHECK(Configure(&g_mappings[0], path.mapping));
            s_record = false;
            s.accepted = path.accepted;
            s_timedCallback = pathIndex == 1 ? EmptyCallback : MouseHookProc;
            MSLLHOOKSTRUCT mouse = {};
            mouse.mouseData = static_cast<DWORD>(path.button) << 16;
            mouse.flags = path.flags;
            const LPARAM data = reinterpret_cast<LPARAM>(&mouse);
            const bool bracketOnly = pathIndex == 0;
            for (size_t i = 0; i < kWarmup; ++i)
            {
                PrepareSample(path.pending);
                LARGE_INTEGER begin, end;
                LRESULT result = 0;
                QueryPerformanceCounter(&begin);
                for (size_t call = 0; call < batchSize; ++call)
                {
                    if (batched)
                        PrepareSample(path.pending);
                    if (!bracketOnly)
                        result = s_timedCallback(HC_ACTION, path.message, data);
                }
                QueryPerformanceCounter(&end);
                s_result = result;
            }
            for (size_t i = 0; i < kSamples; ++i)
            {
                PrepareSample(path.pending);
                LARGE_INTEGER begin, end;
                LRESULT result = 0;
                QueryPerformanceCounter(&begin);
                for (size_t call = 0; call < batchSize; ++call)
                {
                    if (batched)
                        PrepareSample(path.pending);
                    if (!bracketOnly)
                        result = s_timedCallback(HC_ACTION, path.message, data);
                }
                QueryPerformanceCounter(&end);
                s_result = result;
                ticks[i] = end.QuadPart - begin.QuadPart;
            }
            std::sort(ticks.begin(), ticks.end());
            CHECK(ticks.front() >= 0);
            const double toNs = 1.0e9 / static_cast<double>(frequency.QuadPart) / static_cast<double>(batchSize);
            // Nearest-rank percentiles: ceil(p*N)-1. No overhead subtraction.
            std::printf("%d,%s,%.1f,%.1f,%.1f,%.1f\n", run, path.name,
                        static_cast<double>(ticks[kSamples * 50 / 100 - 1]) * toNs,
                        static_cast<double>(ticks[kSamples * 95 / 100 - 1]) * toNs,
                        static_cast<double>(ticks[kSamples * 99 / 100 - 1]) * toNs,
                        static_cast<double>(ticks.back()) * toNs);
        }
    }
    std::printf("Count-only sink: sends=%llu next=%llu; result=%lld\n",
                s_benchSends, s_benchNext, static_cast<long long>(s_result));
    CHECK(s_benchSends == 3300000 * batchSize && s_benchNext == 2750000 * batchSize);
    return true;
}
}

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && std::strcmp(argv[1], "--tests") == 0))
        return phase0::RunTests() ? 0 : 1;
    if (argc == 2 && std::strcmp(argv[1], "--bench") == 0)
        return phase0::Benchmark(false) ? 0 : 1;
    if (argc == 2 && std::strcmp(argv[1], "--bench-batch") == 0)
        return phase0::Benchmark(true) ? 0 : 1;
    std::fprintf(stderr, "Usage: phase0.exe [--tests | --bench | --bench-batch]\n");
    return 2;
}
