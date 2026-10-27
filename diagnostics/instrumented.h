// SPDX-License-Identifier: MIT
// Include only in a dedicated diagnostic/test translation unit. Production never includes this.
#pragma once
#include "trace.h"
#include <shellapi.h>

static LRESULT CALLBACK MouseHookProc(int code, WPARAM message, LPARAM data);
namespace latency
{
static HookTrace capture;
static UINT activeHook;
// Resolve the underlying API BEFORE substitution, allowing the regression harness's stubs.
static UINT UnderlyingSend(UINT count, LPINPUT inputs, int size) { return SendInput(count, inputs, size); }
static HHOOK UnderlyingInstall(int kind, HOOKPROC proc, HINSTANCE instance, DWORD thread)
{ return SetWindowsHookExW(kind, proc, instance, thread); }
static UINT WINAPI MeasuredSend(UINT count, LPINPUT inputs, int size);
static HHOOK WINAPI MeasuredInstall(int kind, HOOKPROC, HINSTANCE instance, DWORD thread)
{ return UnderlyingInstall(kind, MouseHookProc, instance, thread); }
}
#undef SendInput
#undef SetWindowsHookExW
#define SendInput latency::MeasuredSend
#define SetWindowsHookExW latency::MeasuredInstall
#define MouseHookProc HookBody
#include "../Clicklet.cpp"
#undef MouseHookProc
#undef SendInput
#undef SetWindowsHookExW

static LRESULT CALLBACK MouseHookProc(int code, WPARAM message, LPARAM data)
{
    const LONGLONG begin = latency::Now();
    const UINT parent = latency::activeHook;
    latency::Hook *record = nullptr;
    // Timestamp at wrapper entry; retain only XBUTTON callbacks, including injected/pass-through.
    // Other mouse messages still incur the entry clock read in this diagnostic executable.
    if (code == HC_ACTION && (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP))
    {
        auto &trace = latency::capture;
        if (trace.header.count < latency::kCapacity)
        {
            record = &trace.hooks[trace.header.count++];
            const auto *mouse = reinterpret_cast<const MSLLHOOKSTRUCT *>(data);
            record->begin = begin;
            record->id = trace.header.count;
            record->parent = parent;
            record->button = HIWORD(mouse->mouseData);
            record->message = static_cast<UINT>(message);
            record->flags = mouse->flags;
        }
        else ++trace.header.dropped;
    }
    latency::activeHook = record ? record->id : 0;
    const LRESULT result = HookBody(code, message, data);
    if (record)
    {
        record->end = latency::Now();
        record->result = result;
    }
    latency::activeHook = parent;
    return result;
}

namespace latency
{
static UINT WINAPI MeasuredSend(UINT count, LPINPUT inputs, int size)
{
    Send *record = nullptr;
    if (capture.header.sends < kCapacity && count <= kInputs)
    {
        record = &capture.sends[capture.header.sends++];
        record->hook = activeHook;
        record->requested = count;
        for (UINT i = 0; i < count; ++i)
            record->keys[i] = { inputs[i].ki.wVk, inputs[i].ki.wScan,
                               inputs[i].ki.dwFlags, inputs[i].ki.dwExtraInfo };
        record->begin = Now();
    }
    else ++capture.header.sendDropped;
    const UINT accepted = UnderlyingSend(count, inputs, size);
    // Save/restore the underlying error. No attempt to infer UIPI from it.
    const DWORD error = GetLastError();
    if (record)
    {
        record->end = Now();
        record->accepted = accepted;
        record->error = error;
    }
    SetLastError(error);
    return accepted;
}
}
