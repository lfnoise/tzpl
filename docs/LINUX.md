# TZPL on Linux

Linux is a supported build target for the whole stack: the Tzopilotl
interpreter, the synthdef compiler (including its runtime clang -> `.so` ->
`dlopen` plugin pipeline), the audio engine, the bridges, the headless
`tzpl_app`, and the JUCE GUI app (`tzpl_app_juce`). The Dear ImGui app's GUI
(`app/src/app_gui.mm`) is Metal/Cocoa-only and stays macOS-only; on Linux the
JUCE app is the GUI and `tzpl_app` builds headless.

## Requirements

- **Clang 19+**. Clang is required (not GCC): the engine and generated
  plugins use Clang's `ext_vector_type` swizzle syntax, and the lang VM uses
  `[[clang::musttail]]`. Clang 18 specifically does not work with libstdc++:
  it lacks `__cpp_concepts >= 202002L`, which libstdc++ requires before
  exposing C++23 `<expected>`. Configure with `CC=clang-19 CXX=clang++-19`
  (both in Ubuntu 24.04's noble-updates).
- **libstdc++ from GCC 14+** (package `g++-14` on Ubuntu 24.04). Clang picks
  the newest GCC toolchain installed; libstdc++ 13 lacks C++23 `<print>`.
- **CMake 3.21+**, Ninja recommended.
- Dev packages: `libasound2-dev` (required), `libjack-jackd2-dev` and
  `libpulse-dev` (optional audio backends), `libsndfile1-dev` (required:
  audio file reading), plus the X11/freetype/fontconfig set for the JUCE app
  (see `dev/linux/Dockerfile` for the full list).
- Network access on first configure: Sleef (and JUCE, when enabled) come in
  via CMake FetchContent.

## Building

```sh
CC=clang-19 CXX=clang++-19 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DTZPL_BUILD_APP=ON -DTZPL_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

Tests:

```sh
bash lang/tests/run_tests.sh                        # interpreter golden suite
./build/synthdef-compiler/synthdef-compiler --test  # plugin pipeline end-to-end
bash integration-tests/run_synthc_render_ab.sh      # offline render A/B
```

A few lang golden files have `.expected.linux` overrides: macOS-only libm
entry points (`__sinpi`, `__cospi`, `__tanpi`, `__exp10`) fall back to
portable formulas elsewhere, which shifts a handful of printed values. On
Linux, `run_tests.sh -u` writes `.linux` overrides instead of touching the
macOS-authored `.expected` files.

## Docker workflow (developing from a Mac)

`dev/linux/Dockerfile` provides the reference environment (Ubuntu 24.04,
clang 19 -- same archive as GitHub's `ubuntu-latest` runners). From the repo root:

```sh
docker build -t tzpl-linux dev/linux
docker volume create tzpl-build     # keep artifacts out of the Dropbox mount
docker run -it --rm -v "$PWD":/src -v tzpl-build:/build tzpl-linux
```

Inside the container, build with `-B /build/rel` and point the test runners
at it: `TZPL_BIN=/build/rel/lang/tzpl bash /src/lang/tests/run_tests.sh`,
`TZPL_BUILD_DIR=/build/rel bash /src/integration-tests/run_pink_test.sh`.

## Audio backends

The engine opens RtAudio with automatic API selection: JACK, then
PulseAudio, then ALSA -- whichever was compiled in (present at configure
time) and reports devices. PipeWire systems are covered through its
JACK/Pulse/ALSA compatibility layers. The JUCE app uses JUCE's own device
layer (ALSA/JACK) instead of RtAudio.

## Real-time configuration

The engine's worker threads request `SCHED_RR` and the process calls
`mlockall` to pin memory. Both degrade gracefully (a console message, normal
scheduling) when the limits below aren't granted -- audio still runs, with
weaker xrun guarantees. To grant them, add your user to the `audio` group
and create `/etc/security/limits.d/95-tzpl-audio.conf`:

```
@audio - rtprio 95
@audio - memlock unlimited
```

then log out and back in. (Most distros' JACK/PipeWire packages install an
equivalent file already.)

## Runtime plugin compilation

`defSynth` shells out to a C++ compiler at runtime. The compiler used is, in
order: `$TZPL_CC` if set, else the compiler the build was configured with
(baked in at configure time), else `clang`/`clang++` from `PATH`. Plugins
are written to `$TZPL_BUILD` (default `~/tzpl-build/`) as `.so` files, with
`sleef.h` and `libsleef.so*` staged alongside so the directory is
self-contained.

## Known platform gaps

- The CoreAudio sample-rate-change listener has no Linux equivalent; device
  sample-rate changes under a running stream are not detected.
- The `mouseX`/`mouseY`/`mouseButton` ugens are fed by the JUCE app on
  Linux; under headless `tzpl_app` (`--nogui`) they read 0 (the standalone
  poller is CoreGraphics-based).
- Packaging (`dist` target / DMG) is macOS-only; Linux distribution is
  a plain `cmake --install`.
