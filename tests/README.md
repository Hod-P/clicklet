# Input regression tests and simulated callback benchmark

This standalone console harness includes the current `Clicklet.cpp` and
substitutes recording stubs for SendInput, CallNextHookEx, hook install/uninstall,
tray notification, default window processing and tray menu APIs. It never calls
the application's wWinMain or installs an OS hook. Configuration tests read only
the fixtures copied beside the harness and an explicitly absent test filename;
they do not use the user's Clicklet.ini.

Regression tests cover plain-key holds, duplicate DOWN, independent/shared ownership,
all recognized plain keys, failure pairing, bounded release retries, disable/exit,
reentrant transitions and legacy shortcut batches. An accepted-prefix keyboard
model validates hypothetical input state; it does not emulate Windows, UIPI or
physical keyboard interaction. Two mappings are also checked against all 4^6
six-event sequences, with distinct/shared target keys (8,192 sequences).

Additional regression groups cover failed plain DOWN after a reentrant
UP, recovery interrupted by UP/new DOWN or disable, and shortcut suppression
reservation/rollback for every result 0..4 and 0..10. These tests also check partial
shortcut cleanup reentrancy, nested duplicates, failed repeats, and the existing
plain-hold versus modifier-shortcut collision policy. Sequential shortcut
repeats retain their legacy behavior; nested dispatch is blocked. A zero-result
send forwards the mouse press only if its reserved UP has not already been
consumed. Otherwise both original edges are consumed. The runner reports all
failed groups before returning failure. Test cases and the complete group list
are in [phase0.cpp](phase0.cpp).

Lifecycle regressions send WM_ENDSESSION directly to the window procedure,
covering canceled shutdown, shared/distinct holds and bounded pending-release
recovery. INI fixtures cover truncation that would otherwise accept a different
mapping and complete values just below the truncation boundary. Tray menu stubs
cover cursor lookup failure, cleanup, later success and nested-menu suppression.

From the repository root in PowerShell, using MSBuild from an installation with
v143 and the Windows SDK (adjust the executable path for your installation):

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' Clicklet.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' tests\phase0.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& .\build\phase0\x64\Release\phase0.exe --tests
& .\build\phase0\x64\Release\phase0.exe --bench
& .\build\phase0\x64\Release\phase0.exe --bench-batch
```

The test project imports the production project to inherit v143, C++17,
`/W4 /WX /O1 /MT /GL /Gy /Oi /permissive-` and linker optimization settings.
It replaces only build inputs, output/intermediate paths and the console
subsystem. It does not compile resources or change the production project.
No third-party framework or dependency is required.

The benchmark directly invokes the real callback through a volatile function
pointer, substituting count-only noinline stubs. Each path uses 10,000 warm-up
calls and 100,000 individually timed samples in each of five runs. Path order
rotates per run. Mappings/events/storage are prepared before timing; the
button/ownership/recovery state is prepared before each sample. QPC brackets one invocation;
storage, sink consumption, sorting and printing happen outside the timed
interval. Empty-bracket and empty-callback controls measure timer/harness costs.
Nearest-rank percentiles and worst observed are printed in nanoseconds with
no overhead subtraction. Output includes QPC frequency and compiler version.

The reported interval includes timer/harness overhead and stub execution. It
excludes actual SendInput, downstream hooks, OS hook delivery, physical-device
latency and target-app response. If measurements approach the controls or QPC
resolution, the distribution cannot resolve isolated callback cost. Five runs
describe local variability, not a performance guarantee. For later comparisons,
retain the same toolchain, harness, flags, mappings, machine/load and measurement
method; do not equate different INPUT batch sizes or plain-key protocols.

`--bench-batch` uses 64 invocations per timer interval to improve resolution.
Its p50/p95/p99/worst values describe distributions of **batch means per call**,
including the loop and per-invocation button/ownership/recovery preparation inside the
timed interval. They are not individual callback tail percentiles. Sample
count, warm-up batches and run count remain unchanged; controls use the same
loop. Use the empty-callback control when comparing this mode, because a
bracket-only loop can be optimized away. Do not subtract control percentiles.
The benchmark validates the total count-only dispatch/forwarding counters.

Batch mode includes state preparation even in its controls. An independent plain
DOWN or UP dispatches one INPUT; duplicate plain DOWN events dispatch none.
Shared-key ownership avoids an extra DOWN and postpones UP until the final release. Partial
shortcut recovery can dispatch extra UPs. Raw batch distributions include
setup/protocol/stub work and do not isolate production callback instruction cost.

Historical local reports and baseline binaries are not distributed with this
source tree. Reproduce measurements with the commands above and retain the
toolchain, conditions and output for any comparison. For separate instrumented
hook-to-receiver measurements, safe diagnostic regression tests and their
limitations, see [diagnostics/README.md](../diagnostics/README.md).
