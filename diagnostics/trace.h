// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace latency
{
constexpr UINT kCapacity = 16384;
constexpr UINT kInputs = 10;
constexpr DWORD kVersion = 1;
enum : DWORD { kHookFile = 1, kReceiverFile = 2 };
struct Header
{
    DWORD magic = 0x32414C43; // CLA2, native little-endian x64 format, not a network protocol
    DWORD version = kVersion;
    DWORD kind = 0;
    DWORD headerSize = sizeof(Header);
    DWORD pid = 0;
    DWORD compiler = _MSC_FULL_VER;
    DWORD synthetic = 0; // set by deterministic fixtures; never present these as measurements
    LONGLONG frequency = 0;
    LONGLONG begin = 0;
    LONGLONG end = 0;
    char run[64] = {};
    char scenario[32] = {};
    UINT count = 0;
    UINT sends = 0;
    UINT dropped = 0;
    UINT sendDropped = 0;
    UINT focusLost = 0;
    UINT sequenceErrors = 0;
};
struct Key { WORD vk; WORD scan; DWORD flags; ULONG_PTR extra; };
struct Hook
{
    LONGLONG begin, end;
    LRESULT result;
    UINT id, parent, button, message;
    DWORD flags;
};
struct Send
{
    LONGLONG begin, end;
    UINT hook, requested, accepted;
    DWORD error; // informational only: GetLastError does not identify UIPI
    Key keys[kInputs];
};
struct Received
{
    LONGLONG time;
    ULONG_PTR extra;
    UINT message, vk, scan, extended, repeat, previous, transition;
    DWORD sourceOK, device, origin;
};
struct HookTrace { Header header; Hook hooks[kCapacity]; Send sends[kCapacity]; };
struct ReceiverTrace { Header header; Received events[kCapacity]; };

inline LONGLONG Now()
{
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    return time.QuadPart;
}
inline void Prefault(void *storage, size_t size)
{
    // Volatile page writes cannot be removed as redundant initialization by /GL.
    auto *pages = static_cast<volatile BYTE *>(storage);
    for (size_t offset = 0; offset < size; offset += 4096) pages[offset] = pages[offset];
    if (size) pages[size - 1] = pages[size - 1];
}
inline bool Label(const char *text, size_t capacity)
{
    const size_t size = std::strlen(text);
    if (size == 0 || size >= capacity) return false;
    for (size_t i = 0; i < size; ++i)
        if (!(text[i] >= 'a' && text[i] <= 'z') && !(text[i] >= 'A' && text[i] <= 'Z') &&
            !(text[i] >= '0' && text[i] <= '9') && text[i] != '-' && text[i] != '_') return false;
    return true;
}
inline bool Initialize(Header &header, DWORD kind, const char *run, const char *scenario)
{
    if (!Label(run, sizeof(header.run)) || !Label(scenario, sizeof(header.scenario))) return false;
    header = Header{};
    header.kind = kind;
    header.pid = GetCurrentProcessId();
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) return false;
    header.frequency = frequency.QuadPart;
    strcpy_s(header.run, run);
    strcpy_s(header.scenario, scenario);
    header.begin = Now();
    return true;
}
inline bool OutputPath(WCHAR (&path)[MAX_PATH], const Header &header, const WCHAR *suffix)
{
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    while (length && path[length - 1] != L'\\') --length;
    if (!length) return false;
    return swprintf_s(path + length, MAX_PATH - length, L"%S-%S-%s.bin",
                      header.run, header.scenario, suffix) > 0;
}
inline bool WritePart(HANDLE file, const void *data, DWORD size)
{
    DWORD written = 0;
    return WriteFile(file, data, size, &written, nullptr) && written == size;
}
// Called only after capture has stopped and all handlers have returned. Never overwrite a run.
inline HANDLE NewOutput(const WCHAR *path)
{
    return CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
}
inline bool Save(const HookTrace &trace, const WCHAR *path)
{
    HANDLE file = NewOutput(path);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = WritePart(file, &trace.header, sizeof(Header)) &&
        WritePart(file, trace.hooks, trace.header.count * sizeof(Hook)) &&
        WritePart(file, trace.sends, trace.header.sends * sizeof(Send));
    if (!CloseHandle(file)) ok = false;
    return ok;
}
inline bool Save(const ReceiverTrace &trace, const WCHAR *path)
{
    HANDLE file = NewOutput(path);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = WritePart(file, &trace.header, sizeof(Header)) &&
        WritePart(file, trace.events, trace.header.count * sizeof(Received));
    if (!CloseHandle(file)) ok = false;
    return ok;
}
inline void Record(ReceiverTrace &trace, const Received &event, bool (&held)[256])
{
    if (trace.header.count == kCapacity) { ++trace.header.dropped; return; }
    trace.events[trace.header.count++] = event;
    const bool up = event.message == WM_KEYUP || event.message == WM_SYSKEYUP;
    if (event.vk >= ARRAYSIZE(held)) { ++trace.header.sequenceErrors; return; }
    if (held[event.vk] == !up || event.repeat != 1 || event.previous != (up ? 1u : 0u) ||
        event.transition != (up ? 1u : 0u)) ++trace.header.sequenceErrors;
    held[event.vk] = !up;
}
}
