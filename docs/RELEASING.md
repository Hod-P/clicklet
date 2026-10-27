# Preparing an unofficial fork release

This document concerns [Hod-P/clicklet](https://github.com/Hod-P/clicklet).
Tal Tabakman developed the [upstream Clicklet project](https://github.com/tabakman/clicklet).
This modified fork is not affiliated with or endorsed by the original developer.
Its changes include held-key remapping, shared key ownership, input recovery,
regression tests and replacement geometric artwork. The original MIT notice
remains intact. No ownership of the entire repository or new copyright in
AI-assisted contributions is claimed.

## Windows x64 binary package

Rebuild `Clicklet.vcxproj` with MSVC v143, Release x64, retaining `/W4 /WX`.
Build `tests/phase0.vcxproj` with the same settings and run its `--tests` mode.
See [test instructions](../tests/README.md). Desktop input tests and launching
the application require separate approval during agent-assisted work.

The production build outputs the following to `build/x64/Release/`:

| File | Purpose |
|---|---|
| `Clicklet.exe` | Rebuilt x64 application, including the geometric placeholder icons and About bitmap. |
| `LICENSE` | Unmodified MIT license, including Copyright (c) 2026 Tal Tabakman. |
| `README.txt` | Copy of [RELEASE_README.txt](../RELEASE_README.txt), identifying the fork, origin, changes and usage limits. |

Copy exactly these three files into a fresh staging folder and archive that
folder. The output directory may also contain a personal `Clicklet.ini`;
leave it untouched and exclude it from the package. Verify that the two text
files match their source files byte for byte.
Inspect the rebuilt EXE's icon groups and About bitmap against the current
`res/placeholder*.ico` and `res/about-placeholder.bmp`. Do not package an old
EXE or an entire build folder: ignored earlier binaries may contain the
original artwork. This process needs no installer or additional runtime.
Inspecting or building a package does not authorize uploading it or publishing
a release.

The executable does not embed or distribute Inter, Segoe UI, or other font
files. Segoe UI is requested from Windows; no Microsoft SDK, compiler or system
DLL is copied into the package. The CRT is statically linked by the existing
`/MT` build; distribution remains subject to the builder's Visual Studio license.
The Inter OFL notice is required when bundling the font, not for this three-file
binary package. The original MIT notice also covers the MIT-licensed
placeholder assets; their provenance is in [res/LICENSE.md](../res/LICENSE.md).

## Source and website distributions

Keep the root `LICENSE`, [artwork notice](../res/LICENSE.md), and
[Inter copyright and SIL OFL 1.1 text](assets/fonts/Inter-LICENSE.txt) in source
archives. Source and documentation inherit the project's MIT license except
for separately licensed components. The unchanged bundled
`assets/fonts/inter-latin-var.woff2` remains under OFL, not MIT. Do not sell the
font by itself or remove its notice. No other third-party asset or runtime
dependency is bundled in the current source tree.

The WOFF2 name table identifies Inter Regular, version `4.001;git-66647c0bb`,
with a 2016 Inter Project Authors copyright and an OFL URL. The bundled notice
preserves its inherited 2020 line and adds the 2016 line confirmed by the
[Inter license at that revision](https://github.com/rsms/inter/blob/66647c0bbbe41a850d79d9c76fb13add3378940f/LICENSE.txt).
The font bytes are unchanged. Its original download/subsetting steps were not
recorded, so metadata is evidence of identity, not a complete supply-chain audit.

A website deployment must retain `assets/fonts/Inter-LICENSE.txt` alongside
the font and its visible license link. Retain the original MIT notice for the
HTML/CSS/JavaScript as well. The current footer links to the fork's root
`LICENSE`; an offline website bundle must also include a local copy of that
license. Do not distribute just `index.html` and the font without the notices.

There is no tracked Pages workflow, `CNAME`, or Jekyll configuration. The
`docs/` folder is website source; it does not establish the fork's live Pages
settings. Check Settings > Pages in **Hod-P/clicklet** before integration or
publishing. A push/merge to its configured publishing branch may deploy the
site. The fork's repository homepage must identify a fork site or be empty;
the original developer's site must be labeled as an upstream reference.

## History, name and release review

The ten original image files were removed from the feature tree, not from Git
history. Local `main` may still have the old assets until integration; old
commits, tags, archives, releases and cached web deployments require separate
review. Preserve history and the original artwork's separate copyright.

The Clicklet name and application identifiers are retained. Artwork replacement
and an unofficial-fork notice do not establish trademark clearance, permission
for historical artwork, or a legal guarantee. The Inter notice includes the
font's embedded attribution, but its precise acquisition/subsetting provenance
has not been independently established. Resolve relevant rights/provenance questions
before public distribution, and validate the intended release tree and package
again after integration. No public release is authorized by this document.
