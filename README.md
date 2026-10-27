# Clicklet (unofficial fork)

<img alt="Geometric placeholder icon" src="docs/assets/placeholder.svg" width="48" height="48">

**Map your extra mouse buttons. That’s it.**

Clicklet is a tiny Windows tray app for mapping extra mouse buttons (4 and 5) to keyboard keys or shortcuts. It runs as a single native `.exe` with an optional config file for up to two mappings. Free and open source.

[Fork repository](https://github.com/Hod-P/clicklet) · [Upstream repository](https://github.com/tabakman/clicklet)

## Project origin

This is an unofficial modified fork of Clicklet, originally developed by
Tal Tabakman. The original source code is licensed under the [MIT License](LICENSE).
This fork includes changes to mouse-button handling, held-key remapping, error
recovery and regression tests. It is not affiliated with or endorsed by the
original developer. The [upstream project](https://github.com/tabakman/clicklet)
and its releases are separate from this fork.

## How it works

Run `Clicklet.exe`. It sits in the tray.

By default, Button 5 sends Ctrl+Enter.

Want something else? Add `Clicklet.ini` next to the executable:

```ini
[Mapping]
Button4=ctrl+c
Button5=ctrl+v
```

That’s the whole config.

Leave a button out and it stays normal. No config file means Button 5 defaults to Ctrl+Enter.

Invalid settings are reported at startup.

Save `Clicklet.ini` without a byte-order mark (BOM).

## Use it for

- **Claude Code & AI tools** — put frequently used shortcuts on your mouse
- **Gmail and Outlook** — send with Ctrl+Enter
- **JupyterLab** — run the selected cell with Ctrl+Enter
- **Copy and paste** — Ctrl+C and Ctrl+V
- **Paste plain text** — Ctrl+Shift+V in apps that support it
- **Close a tab** — Ctrl+W
- **Screenshot** — Win+Shift+S

## Supported shortcuts

Each button can send one supported key, optionally combined with Ctrl, Alt, Shift, Win, or any combination of them.

Supported keys: letters, digits, F1–F24, Enter, Tab, Esc, Space, Backspace, Delete, Insert, Home, End, Page Up, Page Down, and arrow keys.

Modifiers are optional, so a bare key such as `f5` is valid.

Bare keys go down when the mouse button is pressed and up when it is released.
For example, `Button4=f6` holds F6 for the physical duration of the Button 4
press; `Button5=f7` independently does the same for F7. These are examples,
not fixed assignments.
Duplicate presses are ignored until release. If both buttons map to the same
bare key, it stays down until both are released. Modifier shortcuts remain
one-shot combinations sent on press. Clicklet does not generate repeat events.

One shortcut per button. No sequences or macros.

## Small by design

- Single native `.exe` under 400 KB
- No installer
- No runtime to install
- No admin rights
- No polling while idle
- Optional config with up to two mappings

## Private by design

- No network connections
- No telemetry
- No logging
- No file or registry writes

The application source is one C++ file; regression tests are in `tests/`.

## Tray

The tray menu shows each mapped button and its shortcut.

Enable or disable Clicklet from the menu, or open the About box.

## Start with Windows

Clicklet doesn't add itself to startup.

To run it when you sign in, add a shortcut to `Clicklet.exe` in:

```text
shell:startup
```

## Limits

Clicklet is intentionally narrow:

- Mouse buttons 4 and 5 only
- One shortcut per button
- No sequences or macros
- No per-app profiles
- No media or volume keys
- Never remaps left, right, or middle click
- Windows UIPI can block input to apps running at a higher integrity level,
  including apps running as administrator
- Injected mouse events are ignored; target apps may reject synthetic input
- Mouse-vendor software can intercept the side buttons
- Release on disable/exit is best effort: Windows can refuse injected input.
  Physical keys/modifiers and a one-shot shortcut using the same target key can
  interfere with a mapped hold; Clicklet does not track the physical keyboard.
- Unsigned, so Windows SmartScreen may warn on first run

## Specs

| | |
|---|---|
| Platform | Windows 11, x64 |
| Size | Under 400 KB |
| Language | C++ / Win32 |
| Dependencies | None to install |
| Install | Portable |
| Admin | Not required |
| Config | Optional `Clicklet.ini` |
| Mappings | Up to 2 |
| Price | Free |
| Code license | MIT |

## License

Clicklet's source code is licensed under the MIT License.

The original [LICENSE](LICENSE), including Tal Tabakman's copyright notice,
must accompany distributions. The build copies it beside `Clicklet.exe`;
include both files when packaging a compiled release. The EXE alone is not
the complete distribution.

The original Clicklet logo and icons are © Tal Tabakman, all rights reserved;
they are not MIT-licensed and have been removed from this fork's current files.
The new generic placeholders are independently generated locally and provided
under MIT. See [artwork provenance and regeneration](res/LICENSE.md).
The website's bundled Inter font retains its separate
[SIL Open Font License](docs/assets/fonts/Inter-LICENSE.txt).

This cleanup does not assert permission or trademark clearance for the retained
Clicklet name. Original graphics may remain in Git history; do not include
historical artwork or old build artifacts in a fork release.
