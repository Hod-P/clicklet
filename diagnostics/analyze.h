// SPDX-License-Identifier: MIT
#pragma once
#include "trace.h"
#include <algorithm>
#include <vector>

namespace latency
{
inline bool ValidHeader(const Header &h, DWORD kind)
{
    return h.magic == Header{}.magic && h.version == kVersion && h.headerSize == sizeof(Header) &&
        h.kind == kind && h.frequency > 0 && h.frequency <= 1000000000000LL && h.begin > 0 && h.end >= h.begin &&
        h.count <= kCapacity && h.sends <= kCapacity &&
        std::memchr(h.run, 0, sizeof(h.run)) && std::memchr(h.scenario, 0, sizeof(h.scenario)) &&
        Label(h.run, sizeof(h.run)) && Label(h.scenario, sizeof(h.scenario));
}
inline bool ReadPart(HANDLE file, void *data, DWORD size)
{
    DWORD read = 0;
    return ReadFile(file, data, size, &read, nullptr) && read == size;
}
inline bool FileSize(HANDLE file, ULONGLONG expected)
{
    LARGE_INTEGER size;
    return GetFileSizeEx(file, &size) && static_cast<ULONGLONG>(size.QuadPart) == expected;
}
inline bool Load(HookTrace &trace, const WCHAR *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = ReadPart(file, &trace.header, sizeof(Header)) && ValidHeader(trace.header, kHookFile) &&
        FileSize(file, sizeof(Header) + trace.header.count * sizeof(Hook) + trace.header.sends * sizeof(Send)) &&
        ReadPart(file, trace.hooks, static_cast<DWORD>(trace.header.count * sizeof(Hook))) &&
        ReadPart(file, trace.sends, static_cast<DWORD>(trace.header.sends * sizeof(Send)));
    CloseHandle(file);
    return ok;
}
inline bool Load(ReceiverTrace &trace, const WCHAR *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = ReadPart(file, &trace.header, sizeof(Header)) && ValidHeader(trace.header, kReceiverFile) &&
        trace.header.sends == 0 && FileSize(file, sizeof(Header) + trace.header.count * sizeof(Received)) &&
        ReadPart(file, trace.events, static_cast<DWORD>(trace.header.count * sizeof(Received)));
    CloseHandle(file);
    return ok;
}
struct Expected
{
    const Send *send;
    const Hook *hook;
    Key key;
    UINT input, candidates = 0, receiver = 0;
};
// Preserve signed cross-process differences in both pair rows and summaries.
// Matching permits receipt one tick before send entry; that is QPC ordering
// uncertainty, not physical negative latency. Never clamp or offset the data.
inline LONGLONG HookToMessageTicks(const Expected &e, const Received &r)
{
    return r.time - e.hook->begin;
}
struct Analysis
{
    bool compatible = false;
    UINT invalid = 0, excluded = 0, failed = 0, partial = 0, requested = 0, accepted = 0;
    UINT missing = 0, ambiguous = 0, unexpected = 0, matched = 0, ordering = 0, sequence = 0;
    UINT oneTickEarly = 0; // matched receipts exactly one tick before SendInput entry
    UINT failedByEdge[2][2] = {};
    UINT missingByEdge[2][2] = {};
    std::vector<LONGLONG> callbacks[2][2], sends[2][2], singleKeySends[2][2], delivery[2][2];
    std::vector<Expected> expected;
    std::vector<int> matches;
    bool Clean(const Header &h, const Header &r) const
    {
        return compatible && !invalid && !failed && !partial && !missing && !ambiguous &&
            !unexpected && !ordering && !sequence && !h.dropped && !h.sendDropped &&
            !r.dropped && !r.sequenceErrors && !r.focusLost && matched != 0;
    }
};
inline bool Signature(const Expected &e, const Received &r)
{
    const bool up = (e.key.flags & KEYEVENTF_KEYUP) != 0;
    return r.sourceOK && r.device == IMDT_KEYBOARD && r.origin == IMO_INJECTED &&
        r.extra == e.key.extra && r.vk == e.key.vk && r.scan == (e.key.scan & 0xffu) &&
        r.extended == ((e.key.flags & KEYEVENTF_EXTENDEDKEY) ? 1u : 0u) &&
        r.repeat == 1 && r.previous == (up ? 1u : 0u) && r.transition == (up ? 1u : 0u) &&
        (up ? (r.message == WM_KEYUP || r.message == WM_SYSKEYUP)
            : (r.message == WM_KEYDOWN || r.message == WM_SYSKEYDOWN));
}
// Conservative mutual-unique candidates, followed by global insertion-order and DOWN/UP validation.
// Deliberately does NOT greedily pair the next message after a missing/extra event.
inline Analysis Analyze(const HookTrace &h, const ReceiverTrace &r, UINT windowMs = 20)
{
    Analysis a;
    if (!ValidHeader(h.header, kHookFile) || !ValidHeader(r.header, kReceiverFile) ||
        h.header.frequency != r.header.frequency || h.header.synthetic != r.header.synthetic ||
        std::strcmp(h.header.run, r.header.run) ||
        std::strcmp(h.header.scenario, r.header.scenario) || windowMs == 0 || windowMs > 1000 ||
        h.header.begin > r.header.begin || h.header.end < r.header.end) return a;
    a.compatible = true;
    const LONGLONG limit = h.header.frequency * windowMs / 1000;
    for (UINT i = 0; i < h.header.count; ++i)
    {
        const Hook &hook = h.hooks[i];
        if (hook.id != i + 1 || hook.parent >= hook.id || hook.end < hook.begin ||
            hook.begin < h.header.begin || hook.end > h.header.end ||
            (hook.message != WM_XBUTTONDOWN && hook.message != WM_XBUTTONUP)) { ++a.invalid; continue; }
        if (hook.begin < r.header.begin || hook.end > r.header.end ||
            (hook.flags & LLMHF_INJECTED) || (hook.button != XBUTTON1 && hook.button != XBUTTON2))
        { ++a.excluded; continue; }
        const UINT b = hook.button - XBUTTON1, edge = hook.message == WM_XBUTTONUP ? 1u : 0u;
        a.callbacks[b][edge].push_back(hook.end - hook.begin);
    }
    for (UINT i = 0; i < h.header.sends; ++i)
    {
        const Send &send = h.sends[i];
        if (send.begin < h.header.begin || send.end < send.begin || send.end > h.header.end ||
            send.requested == 0 || send.requested > kInputs || send.accepted > send.requested ||
            send.hook > h.header.count) { ++a.invalid; continue; }
        if (send.begin < r.header.begin || send.end > r.header.end) { ++a.excluded; continue; }
        a.requested += send.requested;
        a.accepted += send.accepted;
        if (!send.hook) { ++a.invalid; continue; } // lifecycle sends are never ordinary mouse samples
        const Hook &hook = h.hooks[send.hook - 1];
        if (hook.begin > send.begin || hook.end < send.end || hook.begin < r.header.begin ||
            hook.end > r.header.end || (hook.flags & LLMHF_INJECTED) ||
            (hook.button != XBUTTON1 && hook.button != XBUTTON2)) { ++a.invalid; continue; }
        const UINT b = hook.button - XBUTTON1, edge = hook.message == WM_XBUTTONUP ? 1u : 0u;
        a.sends[b][edge].push_back(send.end - send.begin);
        if (send.requested == 1)
            a.singleKeySends[b][(send.keys[0].flags & KEYEVENTF_KEYUP) ? 1 : 0].push_back(send.end - send.begin);
        if (send.accepted != send.requested)
        {
            ++a.failed; ++a.failedByEdge[b][edge];
            if (send.accepted) ++a.partial;
            continue; // a count is not a per-event receipt; no assumed partial-prefix match
        }
        for (UINT j = 0; j < send.requested; ++j)
            a.expected.push_back({ &send, &hook, send.keys[j], j });
    }
    // Reentrant calls can reserve an outer slot before an inner SendInput. Their overlapping
    // insertion order is indeterminate; reject those runs instead of inferring prefix ownership.
    std::stable_sort(a.expected.begin(), a.expected.end(), [](const Expected &x, const Expected &y) {
        return x.send->begin < y.send->begin;
    });
    bool expectedHeld[256] = {};
    const Send *previousSend = nullptr;
    for (const auto &e : a.expected)
    {
        if (previousSend && e.send != previousSend && e.send->begin < previousSend->end) ++a.ordering;
        previousSend = e.send;
        const bool up = (e.key.flags & KEYEVENTF_KEYUP) != 0;
        if (e.key.vk >= 256 || (e.key.flags & ~(KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY)))
        { ++a.invalid; continue; }
        if (expectedHeld[e.key.vk] == !up) ++a.sequence;
        expectedHeld[e.key.vk] = !up;
    }
    for (bool key : expectedHeld) if (key) ++a.sequence;
    a.matches.assign(r.header.count, -1);
    std::vector<UINT> candidates(r.header.count, 0);
    for (UINT j = 0; j < r.header.count; ++j)
    {
        const Received &event = r.events[j];
        if (event.time < r.header.begin || event.time > r.header.end ||
            (j && event.time < r.events[j - 1].time)) { ++a.invalid; continue; }
        for (size_t i = 0; i < a.expected.size(); ++i)
        {
            auto &e = a.expected[i];
            // Microsoft documents +/- 1 tick cross-thread ordering uncertainty.
            if (event.time >= e.send->begin - 1 && event.time - e.send->begin <= limit && Signature(e, event))
            {
                ++e.candidates;
                e.receiver = j;
                ++candidates[j];
                a.matches[j] = static_cast<int>(i);
            }
        }
    }
    int previous = -1;
    for (UINT j = 0; j < r.header.count; ++j)
    {
        int &index = a.matches[j];
        if (candidates[j] != 1 || a.expected[static_cast<size_t>(index)].candidates != 1)
        { ++a.unexpected; index = -1; continue; }
        const Expected &e = a.expected[static_cast<size_t>(index)];
        if (index <= previous) ++a.ordering;
        previous = index;
        const bool up = (e.key.flags & KEYEVENTF_KEYUP) != 0;
        const UINT b = e.hook->button - XBUTTON1;
        a.delivery[b][up ? 1 : 0].push_back(HookToMessageTicks(e, r.events[j]));
        if (r.events[j].time < e.send->begin) ++a.oneTickEarly;
        ++a.matched;
    }
    for (const auto &e : a.expected)
    {
        if (e.candidates == 0) { ++a.missing; ++a.missingByEdge[e.hook->button - XBUTTON1][(e.key.flags & KEYEVENTF_KEYUP) ? 1 : 0]; }
        else if (e.candidates != 1 || candidates[e.receiver] != 1) ++a.ambiguous;
    }
    return a;
}
inline void Distribution(const char *metric, UINT button, const char *edge,
                          std::vector<LONGLONG> values, LONGLONG frequency)
{
    std::sort(values.begin(), values.end());
    if (values.empty()) { std::printf("%s,%u,%s,0,NA,NA,NA,NA\n", metric, button, edge); return; }
    const auto percentile = [&](size_t p) {
        return static_cast<double>(values[(p * values.size() + 99) / 100 - 1]) * 1e6 / static_cast<double>(frequency);
    };
    std::printf("%s,%u,%s,%zu,%.3f,%.3f,%.3f,%.3f\n", metric, button, edge, values.size(),
                percentile(50), percentile(95), percentile(99),
                static_cast<double>(values.back()) * 1e6 / static_cast<double>(frequency));
}
inline void Print(const Analysis &a, const HookTrace &h, const ReceiverTrace &r)
{
    if (h.header.synthetic) std::printf("SYNTHETIC FIXTURE: fabricated ticks for validation; NOT measured input latency.\n");
    std::printf("%s: correlation is conditional on an isolated session; key metadata is unchanged.\n",
                a.Clean(h.header, r.header) ? "CLEAN" : "INVALID/INCOMPLETE (matched subset only; do not claim complete latency percentiles)");
    if (a.oneTickEarly)
        std::printf("QPC_UNCERTAINTY: one_tick_early_receipts=%u; signed intervals retained in pairs and percentiles, not negative physical latency.\n",
                    a.oneTickEarly);
    std::printf("compatible=%u invalid=%u excluded=%u requested=%u accepted=%u failed_calls=%u partial_calls=%u\n"
                "matched=%u missing=%u ambiguous_expected=%u unexpected_or_ambiguous_received=%u ordering=%u expected_sequence=%u\n"
                "dropped_hook=%u dropped_send=%u dropped_receiver=%u receiver_sequence=%u focus_lost=%u\n",
                a.compatible, a.invalid, a.excluded, a.requested, a.accepted, a.failed, a.partial,
                a.matched, a.missing, a.ambiguous, a.unexpected, a.ordering, a.sequence,
                h.header.dropped, h.header.sendDropped, r.header.dropped, r.header.sequenceErrors, r.header.focusLost);
    std::printf("metric,mouse_button,key_or_mouse_edge,n,p50_us,p95_us,p99_us,worst_us\n");
    for (UINT b = 0; b < 2; ++b) for (UINT edge = 0; edge < 2; ++edge)
    {
        const char *name = edge ? "UP" : "DOWN";
        Distribution("callback_mouse_edge", b + 4, name, a.callbacks[b][edge], h.header.frequency);
        Distribution("SendInput_mouse_edge", b + 4, name, a.sends[b][edge], h.header.frequency);
        Distribution("SendInput_single_key_edge", b + 4, name, a.singleKeySends[b][edge], h.header.frequency);
        Distribution("hook_to_message_key_edge", b + 4, name, a.delivery[b][edge], h.header.frequency);
        std::printf("counts,mouse%u,%s,failed_calls=%u,missing_key_events=%u\n",
                    b + 4, name, a.failedByEdge[b][edge], a.missingByEdge[b][edge]);
    }
    std::printf("pair,send_index,input_index,receive_index,hook_id,mouse_button,key_edge,latency_us\n");
    for (size_t j = 0; j < a.matches.size(); ++j) if (a.matches[j] >= 0)
    {
        const auto &e = a.expected[static_cast<size_t>(a.matches[j])];
        std::printf("pair,%zu,%u,%zu,%u,%u,%s,%.3f\n", static_cast<size_t>(e.send - h.sends),
                    e.input, j, e.hook->id, e.hook->button + 3,
                    (e.key.flags & KEYEVENTF_KEYUP) ? "UP" : "DOWN",
                    static_cast<double>(HookToMessageTicks(e, r.events[j])) * 1e6 / static_cast<double>(h.header.frequency));
    }
}
}
