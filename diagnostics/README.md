# Phase 2 diagnostic capture

This is optional development infrastructure. Normal `Clicklet.vcxproj` does
not include or link it. No production source, key INPUT, mapping schema,
suppression, ownership or recovery policy is changed. Do not run the diagnostic
application, install a real hook or perform desktop input tests without approval.

## Verified results and validation scope

Approved real Windows 11 x64 measurements have been completed with this
infrastructure. Plain Button4=F6 / Button5=F7 input was collected using manual
physical side-button presses; no automated desktop input was generated.

| Local session | Receiver events correlated | INPUTs accepted in whole hook session | Worst observed hook-to-receiver interval |
|---|---:|---:|---:|
| Phase 2B | 14 / 14 | 18 / 18 | 0.9385 ms |
| Phase 2C | 1,054 / 1,054 | 1,054 / 1,054 | 0.6122 ms |

Both armed captures had zero missing, ambiguous, unexpected, ordering, sequence,
focus-loss and overflow flags. Phase 2B had four further events after receiver
Stop; those were excluded from receiver validation. Phase 2C retained all 527
cycles (231 Mouse5 followed by 296 Mouse4), exceeding and reversing the planned
100-per-button sequence; all its records were inside the receiver interval.
The retained captures contain no tolerated early receipts or negative intervals.

These are observations from the tested local software workload, not latency
guarantees, physical-button timings or evidence of optimization. The sessions
used different counts/pace, and small, temporally related samples do not establish
extreme-tail p99 or run-to-run variation. Full machine conditions, raw traces,
resource observations and reports (`PHASE2B_REPORT.md`, `PHASE2C_REPORT.md`) remain
local and ignored by Git. They are not distributed with this source tree.

## Build and safe validation

From the repository root in PowerShell, with the installed v143 toolset:

```powershell
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild Clicklet.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& $msbuild tests\phase0.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& $msbuild diagnostics\clicklet-diagnostic.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& $msbuild diagnostics\receiver.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& $msbuild diagnostics\analyze.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& $msbuild tests\latency.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& .\build\phase0\x64\Release\phase0.exe --tests
& .\build\latency\x64\Release\latency.exe
& .\build\latency\x64\Release\latency.exe --overhead
```

Every project inherits v143, C++17, `/W4 /WX /O1 /Oi /GL /MT /Gy` and linker
optimizations from the production project. The test entry points replace all
SendInput, hook, next-hook and tray calls with stubs. The latency tests run all
existing regression groups through the measured callback plus correlation,
buffer, file-format and sequence tests. No hook/window is installed by these
tests. Deterministic timestamp regressions cover zero/positive differences,
the tolerated negative-one-tick boundary, earlier invalid receipts and reversed
same-process brackets. `latency.exe --write-fixture` optionally creates two explicitly synthetic
traces beside itself; `analyze.exe` labels them as fabricated validation data.
It refuses to overwrite them. This exercises the CLI without desktop input.

## Architecture and scope

`instrumented.h` compiles the unchanged Clicklet source in a separate target.
It substitutes wrappers for hook registration and SendInput, retaining the
original callback as the wrapper's body. Callback IDs and a saved/restored
parent ID correlate nested dispatches. Each captured SendInput retains its
callback ID, requested/accepted counts, GetLastError and an unchanged copy of
the precomputed keyboard descriptors. Lifecycle sends have ID zero and are
excluded from ordinary mouse samples. Partial batches are recorded honestly;
the analyzer does not assume their returned count identifies an accepted prefix.

The hook buffer holds 16,384 XBUTTON callbacks and 16,384 SendInput calls.
Other mouse messages are not retained. Each receiver buffer holds 16,384
keyboard messages. Pages are zeroed/touched before capture. Buffers never wrap
or allocate. Overflow counts invalidate a run while original dispatch continues.
The hook does only QPC, fixed scalar/descriptor copies, counters and direct calls;
no IPC, queue, waiting, locking, allocation, UI or I/O occurs there. Recording
is on the existing hook/message-loop thread, including reentrant callbacks.
Neither executable adds a thread, timer, polling loop or priority change.

The receiver is a standalone Win32 window. Arm it using its mouse-operated
menu after focusing it, with both buttons and all keyboard keys released. It
captures WM_KEYDOWN/WM_KEYUP and WM_SYSKEYDOWN/WM_SYSKEYUP, QPC first in the key
handler, then scan/extended/repeat/previous/transition bits, GetMessageExtraInfo
and GetCurrentInputMessageSource. Its fixed key-state table detects repeats,
orphan UPs and held keys at Stop. Focus loss stops capture and invalidates the
session. There is one arm interval per process. Keyboard messages are consumed
without TranslateMessage, formatting or I/O. Use Stop after releasing all keys,
then close the window. **Saving happens after the message loop returns**, and
hook traces save after the original Clicklet application returns on tray Exit.
Save failure returns nonzero. Files use CREATE_NEW beside their executables.
No file is saved while a latency-critical handler is running.

## Correlation and output

Use one fresh run ID/scenario per pair on the same Windows boot/machine. Native
x64 trace headers record format/version, compiler, process ID, run/scenario,
QPC frequency, capture interval, counts, overflow and receiver validity flags.
Files contain compact fixed-layout records, not pointers. The reader validates
version, exact file size, bounded counts and timestamps. These files are local
development artifacts, not a stable interchange format.

QPC is a common clock across processes on the same machine. The analyzer
requires the entire receiver interval to fall within the hook session. Samples
outside that arm interval are counted as excluded. It accepts only full-success
SendInput requests with valid parent intervals. A received event must match
the expected VK, scan, extended flag, direction, extraInfo, one-event repeat
and previous/transition state, and be classified as injected keyboard input.
Receive time must be at least send entry minus one QPC tick, and no more than
the chosen candidate window after send entry. Receipt before SendInput returns
is legal. The default window is 20 ms, configurable from 1 through 1,000 ms.

Reporting preserves the **raw signed hook-entry-to-message QPC difference** in
both individual pair rows and percentile/maximum summaries, without clamping
or offsetting. A unique receipt exactly one tick before SendInput entry is
allowed as cross-process clock-order uncertainty. If any such receipt is matched,
output includes `QPC_UNCERTAINTY: one_tick_early_receipts=N`. A negative reported
interval is an ordering/resolution observation, not negative physical latency.
This count is relative to send entry; hook-to-message may remain positive when
the hook began earlier. Receipts more than one tick before send entry cannot
match; missing/unexpected counts make the run incomplete, and no invalid pair
is inserted into its message distribution. Reversed callback or SendInput
brackets are always invalid: the one-tick allowance applies only across processes.

Each candidate must be unique in both directions. It does not pair by nearest
timestamp, blindly zip streams or resynchronize after losses. Multiple plausible
events are reported as ambiguous. Unique matches must preserve global injection
order. Overlapping/reentrant sends have indeterminate insertion order and make
the run invalid. Expected DOWN/UP sequence and final all-released state are also
checked. This supports interleaving and shared keys: a shared join/non-final UP
has no SendInput request and therefore expects no new keyboard message.

```powershell
& .\build\latency\x64\Release\analyze.exe HOOK.bin RECEIVER.bin 20
```

Exit 0 means internally clean correlation, exit 1 means an incomplete/invalid
run, and exit 2 means unreadable/incompatible input or invalid arguments. Output
includes counts, nearest-rank sample count/p50/p95/p99/worst in microseconds and
the validated event pairs. Callback and SendInput distributions are split by
Mouse4/5 and physical mouse DOWN/UP; message intervals are split by Mouse4/5
and **keyboard** DOWN/UP. A shortcut SendInput call can contain both key edges;
its call duration cannot be assigned to just one key edge. Single-INPUT calls
also have their own keyboard DOWN/UP distributions, including recovery UPs
triggered during a mouse DOWN. Do not mix shortcut,
plain-key, failure, shared-key and independent-key workloads in a comparison.
Failed calls (including partial), missing accepted events, ambiguous expected
events, unexpected/ambiguous received events, ordering/sequence/focus errors
and dropped records are reported separately. An invalid run's message
percentiles describe only its matched subset and cannot characterize full
latency. Retain the raw traces and investigate losses instead of dropping them.

Changing the window changes eligible candidates. Prespecify it, keep it fixed
between comparisons and disclose it. Events outside it remain missing/unmatched.
Widening it can introduce ambiguity during rapid presses. A clean result is
conditional on the controlled session: source classification is not a private
Clicklet identity. Another injector with identical metadata can be indistinguishable.
Stop other remappers/macros, do not type target keys, keep the receiver focused,
and use physical side buttons. There is no production injection tag or protocol
change. SendMessage/PostMessage keyboard messages with unavailable provenance
are rejected; no guarantee against a deliberate indistinguishable sender is made.

## Timing overhead and limitations

Callback duration spans the wrapper's first QPC through its final QPC, enclosing
original callback execution, recording and the real SendInput wrappers. It
excludes the final record store, parent restoration, epilogue and the first
QPC's cost before its timestamp. SendInput duration brackets the real API plus
its wrapper call and one GetLastError read. Metadata copies precede that timer;
final stores follow it. The target timestamp includes message queue scheduling
and dispatch to this window's handler. It is software hook-entry-to-handler
latency, **not physical-button-to-target latency**, visual response, game latency,
OS pre-hook delivery cost or a hardware timing reference.

Two successful independent plain edges add eight internal QPC reads per
DOWN+UP pair, bounded record copies and error preservation. Other mouse messages
add an entry QPC read in the diagnostic executable. This can perturb cache,
scheduling and the measured system. `--overhead` rotates five passes of an empty
QPC bracket, eight extra QPC reads (plus volatile sink stores), uninstrumented
callback pair and fully recorded pair, with 10,000 warmups and 100,000 measured
intervals per path/pass. The unchanged second source instance and recorded
instance use the same count-only stubs and compiler flags. Each interval is a
DOWN+UP pair, buffers sweep/reuse their preallocated pages and reset outside
timing. No overhead is subtracted from latency percentiles. This is simulated
instrumentation characterization, not measured real SendInput or input latency.
It does not measure receiver metadata collection's effect on subsequent events.
The overhead mode retains per-pass summaries, not individual timer observations.
Those summaries can be compared, but exact control percentiles cannot be
recomputed from the retained mouse/keyboard traces alone.

Microsoft documents a +/- one-tick ambiguity when ordering QPC timestamps from
different threads. The matcher tolerates one tick at send entry. QPC frequency
is reported; small intervals can be resolution-limited. See
[QPC guidance](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps),
[message source API](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcurrentinputmessagesource)
and [source classification](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ne-winuser-input_message_origin_id).
Message provenance/scan matching was verified in the local Phase 2B/2C sessions;
other machines, layouts and targets still require their own approved validation.
UIPI, driver interception, other hooks, process scheduling and OS refusal remain
platform limits. Missing physical callbacks cannot be counted without an
independent physical reference; this infrastructure detects missing **accepted
keyboard events**, not unobserved physical presses.

## Procedure template for a future approved desktop session

This procedure is separate from the completed results above. Counts, order,
warmups and workloads must match the scope approved for each new session; the
smaller Phase 2B/2C runs did not execute this entire template. Approval must
explicitly cover launching the remapper, installing its hook and remapped
keyboard injection into this receiver. Before then, stop at safe validation.
Do not run either of the following launch commands automatically.

1. Record HEAD, toolchain/flags, OS build, CPU/logical processors, power plan,
   background load, mouse model/connection/report rate, active keyboard layout,
   integrity levels, mappings and other hooks/remappers. Use ordinary priorities
   and the same desktop/session. Confirm no Clicklet instance is already running.
   Leave the user's existing executable/INI untouched. Build using the commands
   above. Create a fresh isolated directory; an existing directory is an error:

   ```powershell
   $session = 'build\phase2a\approved001'
   New-Item -ItemType Directory -Path $session -ErrorAction Stop | Out-Null
   Copy-Item build\latency\x64\Release\clicklet-diagnostic.exe,build\latency\x64\Release\receiver.exe,build\latency\x64\Release\analyze.exe,LICENSE,RELEASE_README.txt -Destination $session
   Set-Content -LiteralPath "$session\Clicklet.ini" -Encoding ascii -Value "[Mapping]`r`nButton4=f6`r`nButton5=f7"
   ```

   This writes a new example INI only in the fresh session directory. F6/F7 are
   test examples, not built-in mappings. Never copy over a personal INI.

2. In separate PowerShell terminals, after approval, launch:

   ```powershell
   & .\build\phase2a\approved001\clicklet-diagnostic.exe approved001 individual
   & .\build\phase2a\approved001\receiver.exe approved001 individual
   ```

   Focus the receiver and do 20 unrecorded warmup taps per side button with all
   keys released afterward. Click Arm capture. Physically press and release
   Mouse4 once, then Mouse5 once as a provenance/sequence smoke test. Release
   both buttons; use Stop capture, then close the receiver. Exit diagnostic
   Clicklet via its tray, which saves the hook trace. Do not switch focus during
   the armed interval. If release is refused, stop and inspect the failure; no
   guarantee that Windows accepts a recovery UP is made.

3. Run the offline analyzer, preserving stdout and exit code:

   ```powershell
   & .\build\phase2a\approved001\analyze.exe .\build\phase2a\approved001\approved001-individual-hook.bin .\build\phase2a\approved001\approved001-individual-receiver.bin 20 | Tee-Object -FilePath .\build\phase2a\approved001\analysis.txt
   $LASTEXITCODE
   ```

   Require clean correlation, two DOWNs/two UPs, matching scan/source records,
   no failure/missing/ambiguous/unexpected/overflow/sequence/focus flags. This is
   infrastructure validation; four events do not establish tail latency.

4. With separate explicit approval for extended benchmarks after a clean smoke
   test, use a new run ID/directory and fresh processes per scenario. A proposed
   extended design is five runs/scenario, 20 unrecorded warmup taps/button/run.
   Target 1,000 measured presses per button per run for individual, rapid and
   alternating workloads; for holds and simultaneous workloads target 200 per
   button per run and disclose the smaller p99 sample size. Stop below buffer
   capacity. Record actual accepted/matched counts rather than promising them.
   No software pacing, synthetic XBUTTONs, sleeps or priority boosts:

   | Scenario | Physical procedure |
   |---|---|
   | Individual | Separated taps on each button, approximately 0.5–1 second between presses. |
   | Holds | Hold one button about 1–3 seconds, release; alternate which is held. Confirm no repeated keyboard DOWN during the hold. |
   | Rapid | Repeated complete taps as quickly as comfortable on one button, then the other; retain ambiguity/loss counts. |
   | Alternating | Mouse4 tap, Mouse5 tap, repeat at a comfortable rapid rate. |
   | Simultaneous | Hold Mouse4, press/hold Mouse5, release Mouse4 then Mouse5; reverse order for the next cycle. |

   An optional shared-key run uses a **new** session INI with both Button4 and
   Button5 mapped to the same key. A join/non-final release has a callback
   sample but no keyboard event; measure message latency only for the first
   DOWN/final UP. Record deviations and reentrant/failure paths separately.

5. Idle CPU/RAM requires the same approval to keep a real Clicklet process
   running. Separately measure normal Release and diagnostic executables, one
   at a time (shared single-instance mutex), receiver unarmed, tray idle/About
   closed. Copy Release into another fresh directory with the same new test
   INI and notices. Warm up manually for ten seconds. From another console:

   ```powershell
   .\diagnostics\idle.ps1 -ProcessIds <EXPLICIT_CLICKLET_PID>
   ```

   Remain idle for at least 60 seconds using an external clock, then press
   Enter in the observer console. The observer samples CPU time and working
   set/private bytes at the two endpoints, without polling, sleeping or
   launching the process. Report one-core and logical-processor-normalized CPU,
   actual elapsed time, and both RAM endpoints (not an invented median/peak).
   Repeat five times and record environmental variation. If required, receiver
   idle usage can be observed by supplying its explicit PID too.

6. Keep raw files plus per-run summary count/p50/p95/p99/worst and all failure/
   mismatch flags, tool versions, process CPU/RAM, and overhead controls. Do
   not pool runs silently or subtract timer percentiles. Keep new observations
   separate from the completed local baseline, and do not infer an optimization
   from stub timing or uncontrolled differences between runs.
