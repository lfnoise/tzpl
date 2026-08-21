# Third-Party Notices

TZPL is licensed under the GPLv3 (see [LICENSE](LICENSE)). This file lists the
third-party software included in the repository or downloaded at build time,
with its license. All of the licenses below are GPLv3-compatible.

## Vendored in this repository

| Component | Path | License | Notes |
|-----------|------|---------|-------|
| [RtAudio](https://github.com/thestk/rtaudio) 6.0.1 | `third_party/rtaudio/` | MIT-style (see `LICENSE` there) | Real-time audio I/O. |
| [oscpack](http://www.rossbencina.com/code/oscpack) | `third_party/oscpack/` | MIT (see `LICENSE` there) | OSC packet manipulation. Locally modified: aarch64/arm64 preprocessor guards added for Apple Silicon. |
| [linenoise](https://github.com/antirez/linenoise) | `lang/third_party/linenoise/` | BSD-2-Clause (see `LICENSE` there) | Line editing for the REPL. |
| [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) | `app/vendor/ImGuiColorTextEdit/` | MIT (see `LICENSE` there) | Code editor widget. Locally modified (undo-chunking patch and other fixes). |
| [md4c](https://github.com/mity/md4c) | `app/vendor/md4c/` | MIT (see `LICENSE.md` there) | Markdown parser. |
| [DejaVu Sans Mono](https://dejavu-fonts.github.io/) | `app/resources/fonts/` | Bitstream Vera Fonts License with public-domain DejaVu changes (see `DejaVu-LICENSE` there) | Editor font. The license restricts use of the "Bitstream Vera" and "DejaVu" names for modified fonts. |
| [tree-sitter](https://github.com/tree-sitter/tree-sitter) runtime headers | `lang/editors/tree-sitter-tzpl/src/tree_sitter/` | MIT (see `LICENSE` there) | Copied by the tree-sitter CLI when generating the parser. The Tzopilotl grammar itself in `lang/editors/tree-sitter-tzpl/` is first-party and licensed MIT so editors can consume it freely. |

## Downloaded at build time (CMake FetchContent)

None of these are included in the repository; they are fetched when the
corresponding build option is enabled.

| Component | Version | License | When |
|-----------|---------|---------|------|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.8 | **AGPLv3** (this project does not use the commercial JUCE license) | `TZPL_BUILD_APP_JUCE=ON` (off by default) |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.0 | MIT | `TZPL_BUILD_APP=ON` (off by default) |
| [GLFW](https://github.com/glfw/glfw) | 3.4 | zlib/libpng | `TZPL_BUILD_APP=ON` (off by default) |
| [SLEEF](https://github.com/shibatch/sleef) | 3.7 | Boost Software License 1.0 | Linux builds of the engine (macOS uses Accelerate) |

## System / optional dependencies

| Component | License | When |
|-----------|---------|------|
| [cnats](https://github.com/nats-io/nats.c) (NATS C client) | Apache-2.0 | `TZPL_BUILD_NATS=ON` (off by default); system/Homebrew install, optionally statically linked |
| [OpenSSL](https://www.openssl.org/) 3.x | Apache-2.0 | Linked via cnats when NATS support is enabled |
| Apple system frameworks (CoreAudio, Accelerate, AudioToolbox, Metal, Cocoa) | Proprietary system libraries | macOS builds (GPLv3 System Library exception) |

## Distribution note (JUCE app)

Binaries built from the `tzpl_app_juce` target combine GPLv3-covered code with
AGPLv3-covered JUCE code, as permitted by section 13 of each license. Anyone
distributing such binaries (e.g. the DMG produced by
`packaging/make_dist_dmg.sh`) must provide complete corresponding source for
the whole combination, including the JUCE version used (pinned above), and the
AGPLv3's network-interaction requirements apply.

## Attributed algorithms (no third-party code)

Some first-party code implements published algorithms with attribution in
source comments: the TLSF allocator (Masmano, Ripoll, Crespo, Real),
xoshiro/xoroshiro PRNGs (Blackman & Vigna, public domain), RBJ Audio EQ
Cookbook biquad filters (Robert Bristow-Johnson), Paul Kellett's pink noise
filter (musicdsp.org, public domain), the SuperCollider PinkNoise algorithm
(Voss-McCartney with Magnus Jonsson's improvement), and the Computer Language
Benchmarks Game programs reimplemented in `benchmarks/lang/` (see the README
there).
