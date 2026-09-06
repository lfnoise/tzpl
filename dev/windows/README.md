# Windows development notes

The reference environment is a plain Windows 10/11 x64 machine (there is no
Docker equivalent of `dev/linux/`): Visual Studio 2022 Build Tools, LLVM for
Windows, CMake, Ninja, Git for Windows, and the pinned llvm-mingw release.
`docs/WINDOWS.md` has the build commands; the GitHub Actions job
`windows-core` in `.github/workflows/tests.yml` is the same recipe on
`windows-latest` and is the fastest way to see the port's state without a
Windows box.

## Verified from macOS with the llvm-mingw cross toolchain

llvm-mingw also ships macOS- and Linux-hosted cross compilers
(`llvm-mingw-<pin>-ucrt-macos-universal.tar.xz`), which is how the port
was checked before any Windows machine touched it:

- Every non-JUCE source file syntax-checks for `x86_64-w64-windows-gnu`
  (all `_WIN32` branches, against real Windows and UCRT headers). This
  found and fixed: two `hash_combine` overloads colliding under LLP64, a
  `std::filesystem::path::c_str()` (wide on Windows) passed as a narrow
  string in the engine loader, `_CRT_RAND_S` defined too late for
  `rand_s`, and `fnmatch(3)` in the plugin tag store.
- Sleef 3.7 cross-builds as a static library with the mingw toolchain
  (`-DNATIVE_BUILD_DIR` pointing at a host build first).
- A real generated `*_synth.cpp` compiles and links into a DLL with the
  exact command lines `synthdef_compile_link.cpp` uses on Windows; the DLL
  exports `load` and `tzpl_abi_version` and imports only KERNEL32 and the
  UCRT api-sets. That is the plugin side of the cross-ABI experiment; the
  host side (clang-cl + `LoadLibrary`) is item 3 below.

Not verifiable without Windows: anything compiled by clang-cl against the
MSVC STL (different headers from mingw's libc++), JUCE, WASAPI at runtime,
the console behaviour, the PowerShell scripts.

To repeat the sweep (from the repo root, with the tarball unpacked to
`$MG` and a mingw Sleef installed to `$SLEEF`, plus a copy of `sndfile.h`):

    CXX=$MG/bin/x86_64-w64-mingw32-clang++
    for f in lang/src/*.cpp engine/src/*.cpp synthdef-compiler/src/*.cpp bridge/src/*.cpp osc/src/*.cpp shared/*.cpp; do
      $CXX -std=c++23 -fsyntax-only -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 \
           -Ishared -I$SLEEF/include -Ilang/src -Ilang/include -Isynthdef-compiler/src -Iosc/include \
           -Iengine/src -Ibridge/include -Ithird_party/oscpack -Ithird_party/rtaudio -Iapp/src "$f" || echo "FAIL $f"
    done

## Verified in CI (windows-latest, clang-cl + llvm-mingw)

Items 1 to 7 of the checklist below pass in the `windows-core` job as of
September 2026: the full build, the ABI layout diff, all 475 golden tests
(one `.expected.windows` override, `qa/edge_complex`, libm drift),
`synthdef-compiler --test` end to end with the mingw toolchain, the doc
and config tests, and `TZPL_JUCE_SELFTEST=1` including the LF-only save
check. Getting there took twelve CI iterations; the fixes are in the
branch history (manifest merge, Sleef include scope and `SLEEF_STATIC_LIBS`,
argument-evaluation order in `parseInt`, NaN spelling, integer division
by zero, POSIX-only calls in the integration tests). Still untested:
audio through a real device (the runner has none) and item 8.

## First-build checklist

The port was written and verified on macOS and Linux (the POSIX paths are
unchanged; the Windows paths are `#ifdef _WIN32` branches and CMake `WIN32`
blocks). The first Windows build should record these, in order, each as a
pass or fail with notes:

1. Toolchains present: `clang-cl --version`; `cl` environment set
   (`INCLUDE`/`LIB`); `<llvm-mingw>\bin\clang++ --version` reports
   `x86_64-w64-windows-gnu`.
2. VM constructs under clang-cl: `lang/src/opcodes.cpp` compiles
   (`[[clang::musttail]]` between the opcode handlers, `__int128`,
   flexible array members in `value.hpp`).
3. Cross-ABI plugin load: `synthdef-compiler.exe --test` end to end
   (codegen, llvm-mingw compile, `LoadLibrary`, `tzpl_abi_version`, write
   through `tzpl_sharedInput`, offline render). Also the layout diff:

       clang-cl /nologo /Ishared shared\tzpl_abi_layout_check.c /Fe:abi_host.exe
       <llvm-mingw>\bin\clang -Ishared shared\tzpl_abi_layout_check.c -o abi_plugin.exe
       abi_host.exe > h.txt & abi_plugin.exe > p.txt & fc h.txt p.txt

   Staged bring-up if the full test fails: `-DCOMPILE_CODE=0` (codegen
   only), then `-DRUN_INTERNAL_AUDIO_ENGINE=0` (compile + link, no load).
4. Sleef builds twice: static with clang-cl (FetchContent, host) and static
   with llvm-mingw (`build/plugin-sleef/`, ExternalProject).
5. RtAudio compiles with `__WINDOWS_WASAPI__ __WINDOWS_DS__` and
   `engine.exe` lists devices.
6. JUCE app builds with clang-cl + Ninja via FetchContent; `juceaide`
   builds; the icon `.rc` compiles; `TZPL_JUCE_SELFTEST=1` prints
   `SELFTEST OK` (the LF-only save self-test included).
7. Console: `tzpl.exe` prints UTF-8 and ANSI colours under conhost and
   Windows Terminal; redirected output is LF-only (the golden suite depends
   on it).
8. Distribution: `packaging\make_toolchain_subset.ps1` self-check passes;
   `cmake --build build --target dist` zips; on a clean VM with no developer
   tools, unzipped to a path with spaces, `Tzopilotl.exe` compiles and plays
   a `defSynth` example.

Things most likely to need adjusting on first contact, by file:

- `shared/tzpl_process.cpp`, `shared/tzpl_dynlib.cpp`, `shared/tzpl_paths.cpp`
  (the Win32 branches were written without a Windows compiler).
- `app/src/gui_state.cpp` (`_pipe` + `PeekNamedPipe` print capture,
  `_beginthreadex` evaluation thread).
- `osc/src/tzpl_osc_server.cpp` (Winsock).
- `engine/CMakeLists.txt` (libsndfile FetchContent options),
  `shared/CMakeLists.txt` (Sleef under clang-cl and under llvm-mingw).
- Golden overrides: expect a few `.expected.windows` files from UCRT libm
  drift, written by `run_tests.sh --update`.
