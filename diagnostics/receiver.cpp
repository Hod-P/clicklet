// SPDX-License-Identifier: MIT
#include "trace.h"

static latency::ReceiverTrace capture;
static bool held[256];
static bool armed, finished;

static void Stop(bool focusLost)
{
    if (!armed) return;
    capture.header.end = latency::Now();
    capture.header.focusLost += focusLost ? 1u : 0u;
    armed = false;
    finished = true;
    for (bool key : held) if (key) ++capture.header.sequenceErrors;
}
static LRESULT CALLBACK Receiver(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (armed && (message == WM_KEYDOWN || message == WM_KEYUP ||
                  message == WM_SYSKEYDOWN || message == WM_SYSKEYUP))
    {
        latency::Received event = {};
        event.time = latency::Now(); // first action in the key handler after dispatch/type checks
        event.message = message;
        event.vk = static_cast<UINT>(wParam);
        const ULONG_PTR bits = static_cast<ULONG_PTR>(lParam);
        event.scan = (bits >> 16) & 0xff;
        event.extended = (bits >> 24) & 1;
        event.repeat = bits & 0xffff;
        event.previous = (bits >> 30) & 1;
        event.transition = (bits >> 31) & 1;
        event.extra = static_cast<ULONG_PTR>(GetMessageExtraInfo());
        INPUT_MESSAGE_SOURCE source = {};
        event.sourceOK = GetCurrentInputMessageSource(&source);
        event.device = source.deviceType;
        event.origin = source.originId;
        latency::Record(capture, event, held);
        return 0; // no formatting, allocation, I/O or UI in keyboard handler
    }
    switch (message)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1 && !armed && !finished)
        {
            SetFocus(window);
            capture.header.begin = latency::Now();
            armed = true;
            SetWindowTextW(window, L"Clicklet receiver - ARMED - physical Mouse4/5 only");
        }
        if (LOWORD(wParam) == 2)
        {
            Stop(false);
            SetWindowTextW(window, L"Clicklet receiver - STOPPED - close to save");
        }
        return 0;
    case WM_KILLFOCUS:
        Stop(true); // never combine samples across focus loss
        return 0;
    case WM_CLOSE:
        Stop(false);
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "Usage: receiver.exe RUN_ID SCENARIO\nUse Capture > Arm, then Stop after releasing both buttons. Close to save.\n");
        return 2;
    }
    ZeroMemory(&capture, sizeof(capture));
    latency::Prefault(&capture, sizeof(capture));
    if (!latency::Initialize(capture.header, latency::kReceiverFile, argv[1], argv[2])) return 2;
    WCHAR path[MAX_PATH];
    if (!latency::OutputPath(path, capture.header, L"receiver") ||
        GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return 2;
    WNDCLASSW wc = {};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = Receiver;
    wc.lpszClassName = L"ClickletLatencyReceiver";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return 2;
    HMENU menu = CreateMenu();
    if (!menu) return 2;
    if (!AppendMenuW(menu, MF_STRING, 1, L"Arm capture") ||
        !AppendMenuW(menu, MF_STRING, 2, L"Stop capture")) { DestroyMenu(menu); return 2; }
    HWND window = CreateWindowW(wc.lpszClassName, L"Clicklet receiver - click Arm capture",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 650, 260,
                                nullptr, menu, wc.hInstance, nullptr);
    if (!window) { DestroyMenu(menu); return 2; }
    ShowWindow(window, SW_SHOW);
    MSG message;
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
        DispatchMessageW(&message); // intentionally no TranslateMessage/accelerators
    if (!finished || result == -1 || !latency::Save(capture, path)) return 3;
    std::printf("Saved receiver=%u dropped=%u sequence_errors=%u focus_lost=%u\n",
                capture.header.count, capture.header.dropped,
                capture.header.sequenceErrors, capture.header.focusLost);
    return 0;
}
