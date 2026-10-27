# Fork placeholder artwork

`placeholder.ico`, `placeholder-disabled.ico`, `about-placeholder.bmp` and
`../docs/assets/placeholder.svg` are new geometric placeholders created for this
unofficial fork. They are provided under the [MIT License](../LICENSE).

The included generator constructs a square border and two circles using only
numeric geometry and colors; it does not read, trace or incorporate upstream
images. These are code-generated placeholders, not a claim of ownership of the
original project or its artwork. No new copyright notice or exclusive rights
are claimed for AI-assisted code or generated outputs; the MIT permission
applies to the extent copyright protection exists.

Reproduce them locally with `python tools/generate-placeholder.py` from the
repository root. The generator uses only Python's standard library; Python is
not required to build or run Clicklet. ICO frames are 16, 20, 24, 32, 40, 48,
64, 128 and 256 pixels, with 32-bit color. The two largest frames use PNG
compression. The enabled tray state shares the executable's icon resource;
the disabled state reverses the dark/light fill and circles.

The original Clicklet logo and icons were marked © 2026 Tal Tabakman,
all rights reserved, and excluded from the source code's MIT License.
Those graphics have been removed from the current project files; they may
remain in Git history. Their replacement does not change their licensing.

The bundled website's Inter font remains covered by its own
[SIL Open Font License](../docs/assets/fonts/Inter-LICENSE.txt).
The Clicklet name is retained; no permission or trademark clearance for that
name is asserted by this artwork replacement.
