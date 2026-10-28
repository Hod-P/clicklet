Clicklet (unofficial modified fork)

Original Clicklet developed by Tal Tabakman.
Original work: Copyright (c) 2026 Tal Tabakman. MIT License; see LICENSE.
Upstream reference: https://github.com/tabakman/clicklet
Fork source and documentation: https://github.com/Hod-P/clicklet
Fork releases: https://github.com/Hod-P/clicklet/releases
This fork is not affiliated with or endorsed by the original developer.

Changes include physical-duration plain-key holds, shared key ownership,
best-effort input recovery, regression tests and custom hand-drawn mouse artwork.
The original logo and icons are not included in this build. Their separate
copyright is unchanged. The Clicklet name is retained without a trademark
clearance claim. No new copyright ownership claim is made here.

Windows 11 x64. Run Clicklet.exe; exit or disable it from the tray menu.
With no Clicklet.ini, Button5 sends Ctrl+Enter. An optional Clicklet.ini beside
the EXE uses [Mapping] with Button4 and Button5 keys. For example:

[Mapping]
Button4=f6
Button5=f7

Plain keys stay down until mouse-button release; modifier shortcuts are sent
once on press. Windows or target applications can refuse injection or release.
Physical keyboard state and mouse-vendor software can interfere. No repeat
events are generated. See the fork documentation for details.

Keep Clicklet.exe, LICENSE and this README.txt together when redistributing.
The custom artwork and its derived assets are provided under the MIT License
by the fork owner. The application does not embed the website font.
If also distributing the website or source tree, retain the bundled Inter
font's separate copyright notice and SIL Open Font License in
docs/assets/fonts/Inter-LICENSE.txt. See docs/RELEASING.md in the source tree.
