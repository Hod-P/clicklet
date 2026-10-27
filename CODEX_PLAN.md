# Clicklet — Codex development plan (low-latency single-key remapping)

Updated: 2026-10-26 (fork/origin/upstream workflow added)
Upstream: https://github.com/tabakman/clicklet
Target: Windows 11 x64, native C++17 / Win32, MSVC.
Status: SPECIFICATION ONLY. No source changes or Windows tests have been performed by this document's author.
Repository strategy: work on a personal GitHub fork (`origin`); retain the author's repository as read-only upstream (`upstream`).

## 1. Product requirements and priority order

**Priority 1 — Minimize actual input latency.** Preserve a direct, short mouse-hook hot path. Avoid speculative optimizations; measure actual cost before and after any change. No sleeps, polling loops, timers, async queues, IPC, allocation, file I/O, GUI updates, parsing, formatting, or logging in the hook callback. No new drivers or services.

**Priority 2 — Correct press/hold/release semantics.** When an XBUTTON is mapped to a plain key, dispatch exactly the corresponding key DOWN when the mouse button goes down and key UP when it goes up. It must be possible to hold the key as long as the mouse button is held. No artificial key repeats. Ignore duplicate/reentrant button-DOWN events until an UP is observed. Handle simultaneous independent buttons, both buttons mapped to the same key, and disabling/exiting while a key is held.

**Priority 3 — No regressions and controlled complexity.** Retain the native, small, portable, single-EXE design, existing tray/UI behavior, and existing shortcut functionality unless a confirmed bug requires changing it. Preserve configured shortcuts (for example `ctrl+c`) as one-shot mappings; give plain keys (for example `f6`) the new press/hold/release behavior. Do not build an elaborate new remapping engine merely to support two buttons.

**Priority 4 — Robust errors, tests, and configuration.** Fail safely when `SendInput` fails, keep XBUTTON DOWN/UP suppression consistent, validate INI input, and verify Release x64 builds and input-state transitions.

### Configurable behavior

Use the existing `Clicklet.ini` schema and supported key names if possible:

```ini
[Mapping]
Button4=f6
Button5=f7
```

`F6`/`F7` are only acceptance-test examples, not hard-coded keys. Support every plain key that Clicklet already recognizes; do not promise arbitrary keyboard scancodes or keys outside the existing parser without explicit scope expansion.

Absent `Clicklet.ini` currently defaults Button5 to `Ctrl+Enter`: preserve that behavior for backwards compatibility. A present INI with both assignments replaces that default. Missing mappings should pass the corresponding original mouse-button events through unchanged.

### Semantics

| Physical event | Config `Button4=f6`, `Button5=f7` | Result |
|---|---|---|
| Mouse4 DOWN | F6 DOWN | Suppress original Mouse4 DOWN only after successful injection |
| Mouse4 UP | F6 UP | Suppress matching original UP; attempt release even if mapping toggled off |
| Mouse5 DOWN | F7 DOWN | Independent of Mouse4 |
| Mouse5 UP | F7 UP | Independent of Mouse4 |
| Rapid repeated Mouse4 DOWN without UP | No duplicate synthetic F6 DOWN | Keep existing pressed state |
| Both mapped to F6 | Shared ownership/reference count, not premature F6 UP | Release F6 only after last owning button is released |

Input injection is subject to Windows UIPI and target-app behavior. True hardware/firmware-like behavior cannot be guaranteed with `WH_MOUSE_LL` + `SendInput`, especially for elevated programs and some games.

## 2. Phase -1 — Fork, remotes, and safe branch setup (P0, before Codex edits)

This is a **manual repository setup** procedure for the user, not an instruction for Codex to modify GitHub or Git remotes autonomously.

### Preferred approach: personal GitHub fork + clone

1. On GitHub, open https://github.com/tabakman/clicklet and select **Fork** to create `YOUR_USERNAME/clicklet`.
2. On the machine where Codex CLI or the VS Code extension will operate, execute:

```powershell
git clone https://github.com/YOUR_USERNAME/clicklet.git
cd clicklet
git remote add upstream https://github.com/tabakman/clicklet.git
git remote -v
git status -sb
git log -1 --oneline
```

3. Ensure the remotes have the intended roles:
   - `origin` = `https://github.com/YOUR_USERNAME/clicklet.git` (your fork; future authorized pushes go here).
   - `upstream` = `https://github.com/tabakman/clicklet.git` (the original; fetch updates here, **do not push**).
   - `git remote add upstream` only records a remote URL. It does **not** clone, fetch, merge, or synchronize anything.
4. Start a separate implementation branch from the checked-out default branch (normally `main`):

```powershell
git switch -c feature/low-latency-remapping
```

5. Put `AGENTS.md` and `CODEX_PLAN.md` in the repository root. Use the provided `Clicklet.ini` only as a test example; do not overwrite an existing personal configuration without reviewing it. Confirm `git status -sb` before handing the workspace to Codex.

If using **Codex Cloud**, connect/select **your fork** as the repository and create/use an isolated working branch. Do not clone a second repository inside an already provisioned workspace. Verify the actual branch and remotes because cloud checkout behavior may differ from local clone.

If the original repository was already cloned, **do not run `git remote add origin` blindly**: `origin` usually already exists and points at the author's repository. Either clone the fork afresh or explicitly review and reconfigure the existing remotes with user approval.

### Upstream updates — explicit manual operation, not automatic

Before or between phases, **only when the user explicitly wants to synchronize**, and only with a clean working tree:

```powershell
git fetch upstream
git switch main
git merge --ff-only upstream/main
```

This updates the **local** `main` only if a fast-forward merge is possible; it does not update the GitHub fork. If fast-forward fails, stop, investigate divergence, and ask the user before choosing a merge/rebase strategy. If the user approves publishing the updated `main` to their fork:

```powershell
git push origin main
```

To incorporate the updated main into the feature branch, get explicit approval first, then use a reviewable merge (`git switch feature/low-latency-remapping` followed by `git merge main`) or an agreed alternative. Resolve conflicts deliberately. **Never force-push, reset/discard changes, automatically merge upstream into the feature branch, or push to upstream.** `git fetch upstream` alone is safe and does not modify local files; `git merge` may change the current branch, so it must not be automated without consent.

**Phase -1 acceptance:** branch is not `main`, `origin` points to the personal fork, `upstream` points to the original repository, and the working tree is in a known state. Record the HEAD commit as the baseline. Do not claim synchronization occurred unless commands actually ran.

## 3. Development workflow / limits

1. Read `AGENTS.md`. Check `git status -sb`, `git remote -v`, and record current HEAD/branch before edits. Work only on the phase explicitly requested. Never work directly on local `main` or modify `upstream/main` (the remote-tracking ref). Check that `origin` is the personal fork and `upstream` is the original; if not, report and stop before production edits.
2. Inspect current `Clicklet.cpp`, `README.md`, `Clicklet.ini.sample`, `.vcxproj` and tray/hook startup code. Confirm source behavior and any proposed defects rather than trusting earlier conversation summaries.
3. Baseline-build Release x64 with MSVC when available. Do not claim a Windows build or timing benchmark ran if the environment lacks Windows/MSVC.
4. Make small reviewable changes with deterministic tests. Prefer extracting a tiny pure state machine plus an injectable `SendInput` test seam. Don't add third-party testing frameworks just for this project.
5. Never stage, commit, push, publish, open a PR, modify Git remotes, fetch/merge/rebase external changes, or overwrite a real user's config unless specifically requested. Never push to `upstream` or force-push. Never trigger real keyboard/mouse injections in automated tests without explicit approval.
6. At the end of each phase report: confirmed source facts vs assumptions, changed files, exact test commands/results (PASS/FAIL/NOT RUN), performance data and methodology (if collected), risks, `git diff --stat`, `git status`.

## 4. Implementation phases

### Phase 0 — Baseline, acceptance matrix, and measurement design (P0)

- Verify that the checkout is on the dedicated feature branch, the `origin`/`upstream` remotes have the expected roles, and the baseline HEAD is recorded. Do not change remotes or synchronize branches during Phase 0.
- Inspect the hook and parser. Confirm that the current implementation sends `keydown`+`keyup` together on XBUTTON DOWN for plain keys; confirm current state when button UP is received.
- Explain existing default config, injected-event filtering, enable/disable behavior, and Win32 permissions limitations.
- Verify that F6 and F7 are currently recognized by the configuration parser; do not rewrite parsing if already sufficient.
- Run an unmodified MSVC Release x64 build if the toolchain is available.
- Identify the smallest test seam for recording hypothetical `SendInput` calls and suppression choices without touching actual Windows input.
- Define a reproducible microbenchmark that distinguishes (a) hook callback execution / `SendInput` dispatch cost from (b) physical button-to-target-app latency. Report both only when independently measured; do not present one as the other.
- Capture baseline idle CPU/RAM/EXE size and, if measurable, p50/p95/p99 callback timings. Benchmark builds must use comparable compiler flags and the same machine/load conditions. Never introduce permanent instrumentation in the release hook.

Acceptance: baseline described, real vs unrun measurements clearly separated, a practical low-overhead test strategy proposed; **do not implement production changes yet**.

### Phase 1 — Single-key hold mode (P0)

- Reuse pre-parsed mapping data but prepare separate `INPUT` structures for DOWN and UP at config load/startup; no key-name lookups or `MapVirtualKeyW` calls in the hook.
- On first XBUTTON DOWN, send at most one single-key DOWN via `SendInput(1, ...)` and track ownership only when that event was successfully injected.
- On matching XBUTTON UP, send at most one single-key UP when the key's ownership count reaches zero. Keep mapping ownership and mouse suppression pairing coherent.
- Deduplicate repeated XBUTTON DOWN events. Ensure Button4 and Button5 are independent; handle the case where both map to the same key via small explicit ownership accounting, rather than releasing on the first UP.
- Preserve legacy one-shot modifier shortcuts without reinterpreting them as long-held key combinations in this phase. Do not extend the feature set to macros, profiles, or unsupported keys.
- Keep a short direct hot path. No new threads/timers or queues solely for single-key mapping. Avoid complicated lock-based state in the callback.

Unit tests: DOWN/UP order, repeated DOWN, unmatched UP, fast taps, interleaved buttons, both mapped to same key, unmapped pass-through, injected XBUTTON filtering, legacy shortcut dispatch unaffected.

Acceptance: plain-key hold works as designed; tests validate emitted events and original XBUTTON suppression.

### Phase 2 — Failure handling, lifecycle, and edge cases (P0)

- Check the return value of every relevant `SendInput` call; a single-key DOWN/UP has a simple 0-or-1 success model. Avoid considering a failed DOWN as successful mapping.
- When DOWN injection fails, pass the original XBUTTON DOWN through, and preserve matching UP pass-through. On failed UP injection, attempt bounded best-effort recovery without blocking/spinning; document residual unavoidable platform failure modes. Never claim guaranteed release if the OS refuses injection.
- Define behavior when tray disable, hook uninstall, config reload (if one exists), or shutdown occurs with one or both mapped keys held. Attempt synthetic UP for owned keys and keep original mouse-event suppression pairing as consistent as possible. Explicitly document cases where shutdown means further XBUTTON events cannot be intercepted.
- Verify interactions with physically held target keys and other physical modifiers. Do not quietly release physical keyboard keys; document the limitations of `SendInput` and the chosen ownership strategy. Do **not** add a global keyboard hook without a demonstrated requirement and approval.
- Confirm whether `UnhookWindowsHookEx` and toggling the hook need any changes; do not introduce a dedicated hook thread unless baseline evidence suggests a measurable responsiveness or timeout benefit.

Unit tests: injected DOWN failure, injected UP failure, repeated and unmatched mouse events, toggling enabled between down/up, shutdown with held key, same-key shared ownership, legacy shortcut failure path.

Acceptance: no avoidable stuck synthetic keys under tested transitions, deterministic suppression policy, documented handling of Windows injection failures.

### Phase 3 — INI validation and documentation (P1)

- Check how `GetPrivateProfileStringW` handles long values; reject truncated or malformed inputs rather than silently interpreting their prefix.
- Preserve the existing recognized key names, optional configuration and fallback `Ctrl+Enter`; keep parser work off the hook path.
- Update README / sample INI for plain-key hold behavior, including `Button4=f6` and `Button5=f7` examples and exact limitations (UIPI, injected events, vendor drivers, absence of keyboard auto-repeat guarantee).

Acceptance: valid/invalid config tests pass; original shortcut configuration still functions as before.

### Phase 4 — Latency profiling and targeted optimization (P0 after correctness)

- Produce repeatable measurements on real Windows hardware for before/after versions: callback execution time, `SendInput` execution time, and if possible independently observed target application reaction time.
- Collect sufficient samples and report sample size, warm-up, machine/load, build configuration, p50/p95/p99 and worst-observed values. Report instrumentation overhead and avoid overinterpreting sub-millisecond differences.
- Examine noisy long-tail spikes; do not claim improvement based on only the average. Compare like-for-like paths: a one-shot shortcut's old two-input DOWN dispatch is not directly comparable to the new held-key protocol without qualification.
- Optimize only demonstrated bottlenecks. Typical candidates: branch count and unnecessary work on mousemove vs XBUTTON events; data precomputation; message-loop stalls. Never change thread priority, use busy-waiting, create a driver, or add an independent hook thread merely for theoretical speed.
- Remove or compile out measurements from Release unless explicitly enabled for diagnostics; verify idle CPU is near zero, and no unexpected memory/EXE-size growth.

Acceptance: no measured material performance regression, or document a measured/justified correctness-versus-performance tradeoff. No unsupported millisecond guarantees.

### Phase 5 — Final validation / release candidate (P1)

- Clean Release x64 build with MSVC `/W4 /WX`; automated unit tests; independent review for races, Win32 API failures, key-state ownership and suppression semantics.
- Manual tests on Windows with safe application (for example Notepad or a small local key-event viewer). Ask for approval before running tools that actually inject input on the user's desktop.
- Report each manual scenario as PASS / FAIL / NOT RUN; verify README behavior and no new telemetry, network, background services, updater, installer or autostart.
- Provide summary of changed files, known limitations, actual benchmark results, and remaining risks; stop for user sign-off before commit/release.

## 5. Mandatory manual test scenarios

1. INI `Button4=f6`, `Button5=f7`: taps and holds, proper keydown/keyup, rapid repeated clicks.
2. Hold Mouse4; tap Mouse5 repeatedly; release Mouse4; then reverse the order.
3. Map both buttons to F6; press both, release one, verify F6 stays held until the second is released.
4. Repeated DOWN events without UP must not trigger repeated DOWN injection.
5. Disable via tray while a button is held; enable again; exit while a button is held. No avoidable stuck keys.
6. Unmapped button must retain native browser back/forward or other original behavior.
7. Try pressing physical F6 during Mouse4=F6 mapping and physically held modifier keys, record supported limitations.
8. Verify legacy `Ctrl+Enter` fallback (no INI) and a configured modifier shortcut still work as designed.
9. Test failure to inject into an elevated foreground app: document UIPI behavior, do not silently claim success.
10. Check normal desktop usage after exiting: physical keyboard/mouse behave normally.
11. Compare idle CPU/RAM and measured latency before/after with methodology and result table.

## 6. Launch prompt for Codex (Phase 0 only)

Read AGENTS.md and CODEX_PLAN.md fully. Our goal is low-latency mapping of XBUTTON1/XBUTTON2 to independently configurable ordinary keys with true DOWN-on-press / UP-on-release. F6/F7 are only example mappings. Minimal input latency is the top priority, but no avoidable stuck keys or regressions are acceptable.

Do Phase 0 only: verify the active feature branch, HEAD, clean/dirty state, and remote roles (`origin` = my fork, `upstream` = original); inspect the actual checked-out source and Git state; verify current behavior and defaults; attempt an unmodified Windows x64 Release build if possible; design a minimal test seam and a valid latency baseline/benchmark methodology. Do not implement the behavior change yet. Distinguish observed facts from assumptions and executed checks from tests not run. Do not change remotes, fetch/merge/rebase, stage, commit, or push. Finish with a concise report and stop for review.

## 7. References

- Original repository: https://github.com/tabakman/clicklet
- SendInput: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- LowLevelMouseProc: https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc
- GetAsyncKeyState: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate
