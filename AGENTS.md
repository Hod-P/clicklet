# Clicklet — instructions for Codex

Read `CODEX_PLAN.md` before work. Work only on the currently requested phase. Inspect `git status -sb` and `git remote -v`, and record the HEAD commit before modifications. Confirm the active branch is a dedicated feature branch, `origin` refers to the user's fork, and `upstream` refers to the author's repository. If that setup does not match, report it and request guidance rather than changing remotes yourself. Never trust suspected issues without checking the actual checked-out source.

## Engineering priorities

1. **Minimal real-world input latency (top priority).** Keep `WH_MOUSE_LL` callback work minimal. No sleeping, polling, timers, asynchronous queues, UI work, locks, dynamic memory, parsing, filesystem I/O, string formatting, or logging in the hook hot path. Precompute Win32 input structures at startup. Do not introduce threads/drivers/services or speculative optimizations without measurements and approval.
2. **Correct key ownership and state transitions.** Plain-key mappings emit key DOWN on physical XBUTTON DOWN and key UP on physical XBUTTON UP. Support holding, interleaved buttons, same-key mappings, repeated down events, and safe shutdown/disable handling. Handle `SendInput` failures honestly. No claims of guaranteed injection across UIPI or a guarantee against OS refusal to release.
3. **Preserve compatibility and small size.** Windows 11 x64, native C++17/Win32, MSVC `/W4 /WX`, portable EXE, no new runtime dependencies. Keep existing one-shot modifier shortcuts and no-INI default `Ctrl+Enter`, unless an explicitly approved change is necessary. F6/F7 are test examples only; use existing INI keys Button4 and Button5.
4. **Test and measure.** Use small deterministic tests with injected Win32-call stubs rather than triggering actual system input during automated tests. Test actual desktop input only with approval. Record p50/p95/p99 with method and scope, distinguish callback runtime from end-to-end latency. Never invent successful builds or benchmark results. Preserve warnings-as-errors.

Do not add telemetry, background services, autostart, network access, persistent logs, an installer or elevation requirement. Do not change artwork/icon licensing. Do not modify files outside the repository or overwrite the user's real INI.

Never stage, commit, push, open PRs, change remotes, fetch/merge/rebase external changes, synchronize with upstream, or publish releases without explicit instructions. Never push to `upstream`, force-push, or directly modify local `main`. `upstream` is for fetching author updates; `origin` is the user's fork. Avoid broad refactors or cosmetic changes.

After each phase report: verified facts vs assumptions; changed files; exact commands and PASS/FAIL/NOT RUN outcomes; measured latency methodology/results (if performed); remaining risks and platform limitations; `git diff --stat` and `git status`. Stop for review before starting another phase.
