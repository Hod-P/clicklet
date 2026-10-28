# Custom Clicklet artwork

The fork owner supplied these original hand-drawn PNGs and authorized their
distribution, and that of their derived assets, under the repository's
[MIT License](../LICENSE):

| Original source | SHA-256 |
|---|---|
| `source/mouse-side-512.png` | `61478b1b03af2c5b53abc1cfc2eeb211ba70401f88cec577c315c49aac9deeb9` |
| `source/tray-enabled-16.png` | `b496394d10ba331fb0658863febe2afc94620e7626508e50415da9afee0ee1a9` |
| `source/tray-disabled-16.png` | `d27980f9061245d2bb5efbee0db71c09c5afc1a74eedf5494b474200934852f9` |

These files are retained unchanged for reproducibility. This notice does not
claim ownership of the original project's name or historical artwork.

## Derived assets and reproduction

From the repository root on Windows, run:

```powershell
powershell -NoProfile -File tools/generate-artwork.ps1
powershell -NoProfile -File tools/generate-artwork.ps1 -Check
```

The generator uses only Windows PowerShell 5.1 and the built-in System.Drawing
APIs. Neither is needed to build or run the portable application. `-Check`
regenerates in memory and compares every output byte without writing files.

- `clicklet-enabled.ico` and `clicklet-disabled.ico` preserve the supported
  16, 20, 24, 32, 40, 48, 64, 128 and 256 pixel sizes with 32-bit RGBA color.
  Each 16px frame contains its original source PNG bytes verbatim, without
  resampling or re-encoding. The remaining frames use the 512px mouse drawing
  with a square in the upper-right eighth of the canvas, matching the 2x2
  indicator at (14, 0) in the 16px designs: Enabled `#00F60B`, Disabled `#AAA8A8`.
  High-quality bicubic downscaling preserves alpha; frames 20 through 64 use
  DIB data with transparency masks, and 128/256 use PNG compression.
- `about-mouse.bmp` places a 56px version of the supplied mouse drawing on the
  existing 260x61, RGB(240, 240, 240) dialog background. The resource and dialog
  dimensions remain unchanged. BMP has no alpha; the source is composited onto
  that background during generation.
- `../docs/assets/mouse-side.png` and `../docs/assets/tray-enabled-16.png` are
  byte-for-byte copies of the supplied originals for the website. The tray
  illustration displays the 16px drawing at its native size; the same PNG is
  the favicon. Header/footer artwork uses the mouse drawing without a status.

The Enabled icon remains resource group 1, shared by the executable and the
enabled tray state; Disabled remains group 3. No tray switching code changes.

## Existing notices

The original Clicklet logo and icons were marked © 2026 Tal Tabakman,
all rights reserved, and excluded from the source code's MIT License.
Those graphics have been removed from the current project files; they may
remain in Git history. Their replacement does not change their licensing.

The bundled website's Inter font remains covered by its own
[SIL Open Font License](../docs/assets/fonts/Inter-LICENSE.txt).
The Clicklet name is retained; no permission or trademark clearance for that
name is asserted by this artwork replacement.
