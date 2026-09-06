# Windows development notes

The reference environment is a plain Windows 10/11 x64 machine (there is no
Docker equivalent of `dev/linux/`): Visual Studio 2022 Build Tools, LLVM for
Windows, CMake, Ninja, Git for Windows, and the pinned llvm-mingw release.
`docs/WINDOWS.md` has the build commands; the GitHub Actions job
`windows-core` in `.github/workflows/tests.yml` is the same recipe on
`windows-latest` and is the fastest way to see the port's state without a
Windows box.

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
