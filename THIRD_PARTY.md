# Third-party dependencies and attribution

Original Der Tondehr Crunchy code is distributed under the MIT License, Copyright (c) 2026 Diego Rodriguez. Dependencies retain their original licenses.

## iPlug2

Plugin and user-interface framework. The compatible revision is pinned in `scripts/setup_dependencies.ps1`. iPlug2 uses a permissive zlib-style license. Its notice is preserved in `docs/licenses/iPlug2-LICENSE.txt` and in the downloaded checkout.

## VST3 SDK

Plugin-format SDK. The compatible revision is pinned in `scripts/setup_dependencies.ps1`; the selected source reports version 3.8.1. It is distributed under the MIT License. Its notice is preserved in `docs/licenses/VST3-LICENSE.txt` and in the downloaded checkout.

## Transitive dependencies

iPlug2 includes or uses components such as HIIR, WDL, RtAudio, RtMidi, NanoVG, and NanoSVG. Their notices remain in the iPlug2 checkout. Anyone distributing binaries must retain the license notices required by the components linked into those binaries.

The triode model reimplements a published mathematical plate-current equation; it does not copy a third-party software library. Parameters, equations, and model limitations are documented in the source code.

This repository does not distribute third-party manuals, circuit drawings, recordings, audio sessions, or impulse responses.

