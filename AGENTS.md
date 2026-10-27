# Clicklet — project guide

## Scope and workflow

- Inspect `git status -sb`, `git remote -v`, branch and HEAD before edits.
- Work on a dedicated branch; `origin` is Hod-P/clicklet and `upstream` is
  tabakman/clicklet. Report unexpected setup and request guidance.
- Check the current source before accepting a suspected defect.
- Keep changes focused on the requested task; avoid broad or cosmetic refactors.
- Never stage, commit, push, fetch, merge, rebase, open PRs, publish, modify
  remotes or change local `main` without explicit authorization.
- Never push to `upstream` or force-push.
- Leave personal INI files, local data and files outside this repository untouched.

## Implementation

- Target Windows 11 x64 with native C++17/Win32, MSVC v143 and `/W4 /WX`.
- Preserve the portable executable and avoid unnecessary dependencies.
- Prioritize minimal mouse-hook latency; prepare INPUT structures at startup.
- No sleeping, polling, timers, queues, UI work, locks, dynamic allocation,
  parsing, filesystem I/O, formatting or logging in the hook hot path.
- Do not introduce threads, drivers, services or speculative optimizations
  without measurements and explicit approval.
- Preserve existing behavior unless a change is explicitly requested, including
  one-shot modifier shortcuts and the no-INI Button5=Ctrl+Enter default.
- Plain keys use DOWN on mouse press and UP on release; maintain shared ownership,
  duplicate suppression and consistent mouse-event pairing across nested events.
- Check SendInput results and preserve bounded recovery for failed/partial sends,
  disable and shutdown. Never promise release when Windows refuses injection.
- Add no telemetry, network access, persistent logs, automatic startup, installer
  or elevation requirement. Preserve original attribution and all licensing,
  including the artwork and font notices.

## Validation and reporting

- Run deterministic regression tests for production modifications; use Win32-call
  stubs, never actual system input, in automated tests. See `tests/README.md`.
- Never launch desktop input injection or install real hooks without explicit
  approval, including launching Clicklet or its diagnostic executable.
- Use `diagnostics/README.md` for separate latency instrumentation. Report sample
  counts, method, scope and p50/p95/p99; distinguish callback time from input latency.
- Do not invent successful builds, tests or measurements. State platform limits.
- Finish with changed files, commands and PASS/FAIL/NOT RUN outcomes, remaining
  risks, `git diff --stat` and `git status`; stop for review.
