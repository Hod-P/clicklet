# Clicklet — Personal Fork

I forked Clicklet for personal use because I wanted to remap two mouse side
buttons without keeping the manufacturer's software running. The original
application was already lightweight. I focused on correctness, held-key
behavior, error recovery and performance measurements instead of speculative
optimization, and deliberately kept the original Win32 architecture.

This is an unofficial fork of [Clicklet](https://github.com/tabakman/clicklet),
originally developed by Tal Tabakman. It is not affiliated with or endorsed by
the original developer. [Fork source](https://github.com/Hod-P/clicklet).

## Changes from upstream

- Plain keys emit KeyDown on mouse press and KeyUp on release, allowing holds.
- Shared-key ownership keeps a key held until the last owning button releases it.
- Duplicate plain-key presses and nested shortcut dispatches are suppressed.
- `SendInput` results are checked, with bounded release retries and recovery
  after failed or partial sends.
- Reentrancy and state-recovery fixes preserve ownership and mouse-event pairing
  across nested events, disable and shutdown.
- Deterministic regression tests use Win32-call stubs without desktop injection.
- Separate latency measurement infrastructure leaves production builds
  uninstrumented.

## Configuration and use

Windows 11 x64. Run `Clicklet.exe` without arguments; it lives in the tray.
With no `Clicklet.ini`, Button 5 sends **Ctrl+Enter** and Button 4 stays normal.
To configure either button, save `Clicklet.ini` beside the executable without
a byte-order mark (BOM):

```ini
[Mapping]
Button4=f6
Button5=f7
```

These are examples, not fixed assignments. A present INI replaces the default:
omitted buttons keep their normal mouse behavior. Empty or invalid mappings,
unknown settings under `[Mapping]`, and a file with no mappings are reported at
startup. Restart Clicklet after editing the file. See also
[Clicklet.ini.sample](Clicklet.ini.sample).

Plain keys stay down for the physical duration of the mouse press. Duplicate
DOWN events are ignored until release; both buttons can independently hold
different keys or share the same key. Clicklet does not generate repeat events.

For one-shot shortcuts, use modifiers followed by one key, separated by `+`:
`ctrl+c`, `ctrl+v`, `ctrl+shift+v`, or `win+shift+s`. Modifiers are Ctrl
(`control`), Alt, Shift and Win (`windows`), in any combination. Shortcuts send
their full press/release sequence on mouse press; they do not hold modifiers.
Sequential shortcut DOWN events retain the original repeat behavior.

Key names are case-insensitive. Supported keys are letters A–Z, digits 0–9,
F1–F24, `enter` (`return`), `tab`, `esc` (`escape`), `space`, `backspace`,
`delete` (`del`), `insert` (`ins`), `home`, `end`, `pageup` (`pgup`),
`pagedown` (`pgdn`), `up`, `down`, `left` and `right`.
Only buttons 4 and 5 are remapped: no left/middle/right-click remapping,
media or volume keys, sequences, macros or per-application profiles.

The tray menu lists mappings and provides Enabled, About and Exit actions.
Clicklet does not add itself to startup. To start it when signing in, manually
place a shortcut to `Clicklet.exe` in the folder opened with `shell:startup`.

## Performance

The verified Phase 2C session used manual physical side-button presses on
Windows 11 x64 with `Button4=f6` and `Button5=f7`. The separate diagnostic
build timestamped hook entry and receipt in a controlled keyboard-message
handler using QueryPerformanceCounter (QPC).

| Observation | Result |
|---|---:|
| Physical press/release cycles | 527 (296 Mouse4, 231 Mouse5) |
| Keyboard events correlated | 1,054 / 1,054 |
| Median hook-to-receiver software latency | Approximately 0.13 ms |
| Worst observed hook-to-receiver latency | 0.6122 ms |
| Observed correlation or ordering errors | 0 |

Per-button/edge medians ranged from 0.1241 to 0.1317 ms. These instrumented
software measurements include scheduling and message dispatch, exclude device
and pre-hook delay, and are **not physical-button-to-application latency**.
No direct benchmark comparison with upstream Clicklet or Razer Synapse was
performed. A single measurement session does not establish guaranteed tail
latency. No production optimization was justified by the measurements.
See [diagnostic methodology and limitations](diagnostics/README.md) and
[simulated callback benchmarks](tests/README.md).

## Build and test

Clicklet is a portable native C++17/Win32 application with no installer or
additional runtime to install. Build on Windows with MSVC v143, the Windows SDK
and MSBuild. From the repository root in PowerShell, adjust the MSBuild path
for your installation:

```powershell
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild Clicklet.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& $msbuild tests\phase0.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
& .\build\phase0\x64\Release\phase0.exe --tests
```

The production output is `build/x64/Release/Clicklet.exe`. Projects retain
`/W4 /WX` and static CRT linking (`/MT`). Regression tests use recording stubs;
they install no hook and generate no desktop input. See
[test coverage and benchmark commands](tests/README.md) and
[separate diagnostic builds and safe tests](diagnostics/README.md).
Launching Clicklet or the diagnostic receiver for desktop input testing during
agent-assisted work requires explicit approval.

## Windows and input limitations

- Windows UIPI can block injection into higher-integrity applications, including
  applications running as administrator. Clicklet does not require elevation.
- Target applications may reject synthetic input; mouse-vendor software or other
  hooks can intercept side buttons. Injected mouse events are ignored.
- Release on disable or exit is best effort: Windows can refuse `SendInput`,
  including recovery releases. Removing the hook also prevents interception of
  subsequent mouse releases.
- Clicklet does not track the physical keyboard. Physically held keys/modifiers,
  or a one-shot shortcut using the same target key, can interfere with a hold.
  Partial shortcut recovery assumes the accepted input count is a prefix of
  the requested batch; this is a recovery model, not a Windows guarantee.
- Unsigned builds may trigger a Windows SmartScreen warning.

The production application adds no telemetry, network access, persistent logs,
background service or polling loop. Diagnostic captures are separate,
explicitly run development tools.

## Attribution, artwork and packaging

Source code is licensed under the [MIT License](LICENSE), including the original
Copyright (c) 2026 Tal Tabakman notice. Preserve that notice when redistributing.

The original logo and icons are © Tal Tabakman, all rights reserved, and are
not MIT-licensed. They have been removed from the current tree but may remain
in Git history. Current geometric assets are independently code-generated
**placeholders**, provided under MIT; they are not final artwork. See
[artwork provenance and regeneration](res/LICENSE.md). The website's Inter font
retains its separate [SIL Open Font License](docs/assets/fonts/Inter-LICENSE.txt).
The retained Clicklet name carries no claim of permission or trademark clearance.

For a binary package, rebuild and put exactly `Clicklet.exe`, `LICENSE` and
`README.txt` together. The build copies the license and
[fork distribution notice](RELEASE_README.txt) beside the EXE. Exclude personal
INI files, historical artwork and old build artifacts. Follow
[release packaging and website notices](docs/RELEASING.md) for checks and
source/website distribution requirements.
