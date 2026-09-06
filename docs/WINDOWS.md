# TZPL on Windows

Windows (x64) is a build target for the whole stack: the Tzopilotl
interpreter, the synthdef compiler (including its runtime compile -> `.dll`
-> `LoadLibrary` plugin pipeline), the audio engine, the bridges, OSC, and
the JUCE app (`tzpl_app_juce`, which is the GUI; the Dear ImGui app is
macOS-only, as on Linux).

Status: verified in CI on `windows-latest` (the `windows-core` job): the
whole tree builds with clang-cl, all 475 interpreter golden tests pass, the
runtime plugin pipeline compiles, links, loads, and renders every test
synth with the llvm-mingw toolchain, the ABI layout matches between the
two compilers, and the JUCE app's headless self-test passes. Not yet
exercised: a real audio device, and the distribution zip on a clean
machine (`dev/windows/README.md`). There is no packaged Windows release
yet.

## Two toolchains

Windows is the one platform where the host binaries and the runtime-compiled
plugins use different compilers:

- **Host** (`Tzopilotl.exe`, `tzpl.exe`, the engine, the compiler):
  **clang-cl**, Clang with the MSVC ABI, against the Windows SDK and the
  MSVC STL. This is the configuration JUCE supports. GCC and MSVC's own `cl`
  are not usable: the engine needs Clang's `ext_vector_type` swizzles and
  the VM needs `[[clang::musttail]]`.
- **Plugins** (`defSynth` at runtime): **llvm-mingw**, Clang + lld + libc++
  with the mingw-w64 UCRT runtime. The Windows SDK and MSVC STL cannot be
  redistributed, but llvm-mingw can, so a trimmed copy ships inside the
  distribution folder as `toolchain/` and plugin compilation works on a
  machine with no developer tools installed.

The two meet only at the plugin ABI (`shared/tzpl_plugin_abi.h`), which is
pure C: the same x64 calling convention and struct layout on both sides
(`shared/tzpl_abi_layout_check.c` proves it; CI diffs its output from both
compilers). Plugins link libc++ and Sleef statically and import nothing but
the system CRT. Plugin memory is allocated and freed inside the plugin.

## Requirements

- **Visual Studio 2022 Build Tools** with the "Desktop development with C++"
  workload (MSVC v143 toolset for the STL, Windows 11 SDK). VS 17.10 or
  newer, so the STL has C++23 `<print>` and `<expected>`.
- **LLVM for Windows** (`clang-cl.exe`, `lld-link.exe`, `llvm-rc.exe`),
  Clang 19 or newer.
- **CMake 3.21+** and **Ninja**.
- **Git for Windows**, whose bash runs the golden test suite.
- **llvm-mingw**, UCRT x86_64 build, for plugin compilation. The pinned
  release is `llvm-mingw-20260826-ucrt-x86_64.zip` (LLVM 23.1.0) from
  https://github.com/mstorsjo/llvm-mingw/releases. Unzip it anywhere.
- Network access on first configure: Sleef, libsndfile, and JUCE come in via
  CMake FetchContent.

## Building

From an "x64 Native Tools Command Prompt" (or after `vcvarsall.bat x64`), so
the SDK and STL paths are set:

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
    -DTZPL_PLUGIN_TOOLCHAIN_DIR=C:\llvm-mingw-20260826-ucrt-x86_64 ^
    -DTZPL_BUILD_APP_JUCE=ON -DTZPL_BUILD_TESTS=ON
cmake --build build
```

`TZPL_PLUGIN_TOOLCHAIN_DIR` also builds Sleef with that toolchain
(`build/plugin-sleef/`) for plugins to link; without it, `defSynth` falls
back to `$TZPL_CC` or a `clang++` on `PATH` and the Sleef staging expects
`lib/libsleef.a` beside `modules/`.

Tests:

```bat
bash lang/tests/run_tests.sh                        :: interpreter golden suite (Git Bash)
build\synthdef-compiler\synthdef-compiler.exe --test :: plugin pipeline end-to-end
build\app\tzpl_doc_tests.exe
set TZPL_JUCE_SELFTEST=1
build\app\tzpl_app_juce_artefacts\Release\Tzopilotl.exe -I "bridge/modules;lang/modules"
```

Path lists (`-I`, `$TZPL_PATH`) use `;` on Windows. The golden suite
compares LF output; `tzpl.exe` writes LF (binary stdout) and the repository's
`.gitattributes` checks the golden files out as LF regardless of
`core.autocrlf`. Residual libm drift gets `.expected.windows` overrides, the
same mechanism as `.expected.linux` (the runner picks the suffix from
`uname`); `run_tests.sh --update` writes them.

## Audio backends

The engine (`engine`, `tzpl_app`, `tzpl`) opens RtAudio with WASAPI, falling
back to auto-selection (WASAPI, then DirectSound) when WASAPI lists no
devices. ASIO is not compiled in: Steinberg's SDK is not redistributable.
The JUCE app uses JUCE's own device layer (WASAPI, DirectSound).

Worker silos request MMCSS "Pro Audio" scheduling plus
`THREAD_PRIORITY_TIME_CRITICAL`; both degrade to normal scheduling with a
console message. There is no memory-pinning step (the Linux `mlockall`
equivalent).

## Runtime plugin compilation

`defSynth` resolves its compiler in this order: `$TZPL_CC`; the bundled
`toolchain\bin\x86_64-w64-mingw32-clang++.exe` (or `clang++.exe`) beside
`modules\`; the toolchain baked in at configure time
(`TZPL_PLUGIN_TOOLCHAIN_DIR`) if it still exists; `clang++` from `PATH`.
Plugins are written to `$TZPL_BUILD` (default `%LOCALAPPDATA%\tzpl-build\`)
as `.dll` files, linked `-static` with `libsleef.a` staged from the
distribution's `lib\` (or the build tree). Plugin headers come from the
distribution's `include\` (or `$TZPL_SHARED_INCLUDE`, or the source tree
for dev builds).

Windows will not delete a DLL that is loaded, so old plugin revisions that
an earlier session could not prune are removed at the next launch.

## Distribution

`cmake --build build --target dist` (with `TZPL_BUNDLE_TOOLCHAIN_DIR`
pointing at a subset made by `packaging\make_toolchain_subset.ps1`)
produces `build\Tzopilotl-<version>-win64.zip`:

```
Tzopilotl\
  Tzopilotl.exe   tzpl.exe   vcruntime140.dll  vcruntime140_1.dll  msvcp140.dll
  modules\  examples\  docs\  editors\  include\  lib\  toolchain\  README.txt
```

Unzip anywhere and keep the folder together. The binaries are not code
signed, so the first launch of a downloaded copy shows SmartScreen's
"Windows protected your PC": click "More info", then "Run anyway".

## Known platform gaps

- No device sample-rate-change listener (as on Linux).
- No ASIO, no NATS, no Dear ImGui app.
- The REPL in `tzpl.exe` has no line editing or history (linenoise is
  termios-only; a stub reads plain lines).
- "Relaunch to apply settings" in the app is not implemented on Windows;
  quit and start the app again.
- Binaries are unsigned (see Distribution).
