# TZPL News

Curated highlights of what has changed, newest first. The full history is
in [git](https://github.com/lfnoise/tzpl/commits/main); this file records
the changes worth knowing about as a user of the platform. Rendered on the
documentation site as the Changelog page.

## v0.3.0 (26 October 2026)

**The block engine** -- the audio engine and the synth compiler were
rebuilt to process audio in blocks (64 samples by default) instead of a
sample at a time. Synths and effects use 1.3x to 3.5x less CPU (an echo
3.5x, a 12-voice plucked string 2.5x, a chorus 1.9x;
`benchmarks/block_engine/`), and timing stays sample-accurate.

**Breaking** -- see [Migrating to the Block Engine](https://lfnoise.github.io/tzpl/Block_Engine_Migration.html)
for what changed and the code to change.

- Plugin ABI v3: synths compiled before this release must be recompiled
  (`defSynthX` does it; the engine refuses old plugins). The ABI's
  `process(synth, offset, n)` runs a block; ports are per-channel buffers.
- `setControl` and control descriptions are gone. A synth's parameters are
  inputs: `control(name, spec)` declares an input with a range and default,
  set with `setInput` / `setInputAt` / `setInputBox` (stepped or ramped),
  `setInputX` (crossfaded) or `impulseInput`, or fed by connecting another
  node's output to it. `ui` widgets attach with `bindInput`.
- Signals are tensors with a shape, and every integer argument of a shape
  operation is an axis: `sum(n)` no longer folds a row into n groups, and
  `transpose(n)` and `mixTo` are removed. A stereo mixdown is a sum along
  an axis (`[l, r] stack sum(-1)`, or a voicer's voices `sum(0)`); `reshape`
  gives a flat row the dims to fold.
- The C++ `synthdef-compiler` is retired; synthc (`defSynthX`) is the only
  synth compiler.

**New**

- Feedback between nodes: a connection may form a cycle (a node feeding
  itself, directly or through others). The cycle runs a sample at a time
  with a one-sample delay, as before, or a block at a time with
  `feedbackMode(silo, FeedbackMode.feedbackBlock)`.
- Voicers skip finished voices: a released voice stops costing CPU once its
  body's `doneWhen(cond)` holds, or once its output has been silent for
  `voicer(n, body, silence)` seconds (default 0.2; 0 turns skipping off).
- Notes are a `notes` input on the voiced synth; `noteSetParams` can ramp a
  sounding note's parameters.
- Shapes and tensor constants: a shape is a `Shape` or its dims (`AsShape`),
  so `control("gains", spec, [4, 2])` and `urand([3, 2])` work; constants
  of any shape are `tensor([2, 3], values)` or nested arrays,
  `tensor([[1.0, 2.0], [3.0, 4.0]])`.
- Shape operations along any axis: `take`, `drop`, `stride`, `slice`,
  `rotate`, `reverse`, `transpose`, `reshape`, `join`, `stack`, `reduce`,
  `scan`, `sum` and kin, `at` and `put`.
- Nested array literals without the inner brackets: a run of n semicolons
  closes n levels of nesting, so `[1, 2, 3; 4; 5, 6]` is
  `[[1, 2, 3], [4], [5, 6]]` and `[1, 2; 3, 4;; 5, 6; 7, 8]` is
  `[[[1, 2], [3, 4]], [[5, 6], [7, 8]]]`. Handy for tensor constants:
  `tensor([1.0, 2.0; 3.0, 4.0])` is a 2x2.
- Values computed from inputs (filter coefficients, pitches) are recomputed
  only when the inputs change, and recurrences such as filters and phasors
  run as block-wide scans or state-space forms; `defSynthXStrict` (or
  `TZPL_STRICT_RECURRENCES=1` for every synth) keeps every recurrence in its
  plain sample order for reference renders.

**Fixed**

- A crash in long renders and live sessions that play scores
  (`music.play`): a list built from a coroutine (`coro toList`, which
  `score` uses) could be freed by the garbage collector before it was
  returned, and the player read it later. Several other places where an
  incremental collection could free a value still in use (`Ref` updates of
  structs, dynamic variables, resumed coroutines, lazy lists) were fixed at
  the same time.
- Building a signal outside a synthdef graph (`let s = fill(...)` at top
  level) is now an error naming the problem instead of a crash.
- Importing two modules that both define a type of the same name
  (`synthdef` and `music.shape` both have `Shape`) and then using the bare
  name is now an "ambiguous type" error that names both modules, instead of
  silently picking one.
- In a voicer whose voices run as SIMD lanes, a view (`take`, `drop`,
  `slice`, ...) of a per-voice value read voice 0's channels for every
  voice.
- A template function, or a function without a declared return type, is
  type-checked when it is first called -- and it was checked seeing the
  caller's local variables. A caller with a parameter named like a
  function the body calls (`chans`, `size`, ...) broke the body with a
  confusing "No matching overload for 'call'"; a body first used inside a
  loop accepted a stray `break`. Such bodies now see only their own scope.
- A top-level `var` is now one variable everywhere. Top-level code used to
  read and write a private copy, so a function's `count = count + 1` was
  invisible at top level, a top-level assignment was invisible to functions,
  and a lambda saw the variable's first value. Scripts that count failures in
  a function and test the count at top level now see the real count.
  Closures over a `var` share it, at top level as inside functions; capture a
  `let` copy for a snapshot.
- Mixing `f32` and `f64` signals in a synthdef no longer fails with
  "intersected to empty". An `f64` phase minus an `f32` oscillator, an
  `f32` control times an `f64` playhead, a comparison or `select2` between
  the two -- all now widen to `f64`, as the SynthDef guide always said, in
  both compilers. The result is exactly what writing the casts by hand
  gives; synthdefs that compiled before are unchanged. (#16)
- A cast on a literal now sticks: `1.5 f64` stays `f64` when the compiler
  folds it into another constant, instead of quietly reverting to `f32`.
- `-2.0 * abs(x)` (any negative constant or control times `abs(x)`) came out
  positive: an algebraic rewrite assumed the factor was non-negative. (#13)
- A file named after a library module that imports it -- a notebook saved as
  `live.x` doing `import live.*` -- now gets an error that says the file
  hides the library module and should be renamed, instead of "Circular
  import detected". Errors at an `import` inside an imported module now also
  point at that module's line. (#15)

## v0.2.6 (24 October 2026)

**Fixed**

- Integer-only operators (`%`, `//`, `&`, `|`, `^`, `<<`, `>>`, `>>>`)
  applied element-wise over arrays, lists, pvecs and tuples silently
  computed `+` instead: `[5, 6] & 3` gave `[8, 9]`. They now apply the
  real operator at any nesting depth, with implicit or explicit `@`
  auto-mapping. Float, Complex or Fraction elements are now a type error
  rather than reaching integer arithmetic, and `//` can be overloaded for
  your own types like the other operators.
- A lambda's declared return type was never checked against its body:
  `fn() S { 140 }` crashed with no diagnostic. It is now a type error,
  for plain and template lambdas and template functions alike, matching
  named functions.
- A synthdef whose output is a constant (`fn() S { 140 scalar }`) could
  not be played or patched -- `play` failed with "engine error 20", and a
  live proxy with "swap bundle rejected". Such outputs are now audio rate
  in both synthdef compilers.

**Changed**

- "No matching overload" errors now cover every piece of syntax that
  dispatches to an overloadable function: binary and unary operators,
  `x[i]`, `x[i] = v`, calling a value, and `<-` / `->`. The message names
  the supplied types, the built-in forms, the overloads you have declared,
  and the declaration that would make the code type-check. Previously some
  of these reported only the built-in form (e.g. "'<-' requires a Ref on
  the left side") even when the real problem was an unmatched overload.

**Docs & Examples**

- The tutorial, FAQ, cookbook and SynthDef guide no longer claim that a
  playing node hot-swaps to a redefined def. Redefining affects only nodes
  started afterwards; use a live proxy (`ndef` / `<-`) to crossfade a
  running sound into a new definition.
- The SynthDefs example notebook gains a mouse-swept noise cell
  (`sweepyNoise`), ported from a sapf one-liner.

## v0.2.5 (14 September 2026)

**Changed**

- Live-coding proxies no longer wait on a compile for anything but your
  own definition. A proxy's anchor (its stable summing point) and monitor
  (its fadeable path to Audio Out) used to be generated synthdefs, each run
  through clang, the linker and `dlopen` once per channel count per
  session -- and the anchor was built three times over by `<-`, `play` and
  `meter` racing into it -- so the first sound from `drone <- ...; drone
  play` arrived some four seconds after the cell. They are now the engine's
  native `_wire<N>` / `_gain<N>` node defs (`audio_engine.wireDef` /
  `gainDef`), registered on first use at any width, so the only thing that
  compiles is the definition you wrote.
- Plugin compiles use a precompiled header of the fixed preamble every
  generated synthdef starts with (the plugin ABI, matrix, random and delay
  headers plus the standard headers), which was most of the cost of a
  compile: a typical synthdef now takes ~60 ms of clang instead of ~500 ms.
  The PCH lives in `~/tzpl-build/pch/`, keyed by a hash of the headers,
  flags, compiler version and the headers' modification times so it
  rebuilds itself after any of those change, and a compile that fails with
  it is retried without it. Net
  effect for `live`: a redefine lands in roughly 200 ms, of which about
  100 ms is macOS's Gatekeeper assessment of the freshly written dylib
  (`syspolicyd` scans every new Mach-O and consults Apple's notarization
  service) rather than anything under our control.

**Changed**

- The scheduling panic (the status bar’s “clear schedulers” button)
  now also stops delay-driven silo actors -- a `spawn`ed actor looping on
  `await delay` (the generative-sequence pattern in Cookbook §7) was left
  running because its timers live in the silo VM’s async queue, not the
  task scheduler. It is also scriptable now as `panicSchedulers()`.

**Fixed**

- A silo module (siloLoad / siloEval) that did `import audio_engine.*`
  failed to compile with `siloLoad: compile error` -- the same for
  `import std.fs.*` and other modules that define NRT wrappers. Importing
  such a module type-checked every wrapper body against the silo's
  real-time restriction, so a wrapper the silo never calls (e.g.
  `loadSampleBank`) failed the whole load. A non-RT-safe function may now
  be *defined* in an imported module compiled for a silo; only an actual
  call to one from silo code is rejected. A companion bug is fixed too: a
  silo's own top-level code was not being RT-checked at all (the persistent
  incremental checker captured its real-time flag before its target was
  current), so a silo that called a non-RT-safe function slipped through --
  it is now correctly rejected.
- `siloLoad` / `siloEval` compile errors are returned in full through the
  result Future, not just printed to the console, so the app's output panel
  shows the actual error (with its source line) instead of a bare
  `siloLoad: compile error`.
- A top-level `await delayReal` / `await delayBeats` in an offline (`--nrt`)
  render script hung forever. The render clock is driven by the render
  thread only after the setup script returns, but the await parked that
  script waiting for the clock to advance -- a deadlock. The render now
  drives its clock (rendering and writing output blocks) while the setup
  script is parked in such an await, so the delay resolves at logical
  render time as documented; an await whose deadline falls past the render
  duration is released when the render ends. (The idiomatic pattern -- a
  render-clock-driven `coro fn ... yield` -- was unaffected.)
- Evaluating `drone <- ...; drone play;` (Cookbook §12.1) produced no sound
  unless the cell also contained an `await`. A fire-and-forget async call --
  which is what `<-` and `play` are -- resolves its background compile on
  the I/O worker thread, but resolving a future only moves its waiters to
  the ready queue; with no top-level `await` anywhere, nothing drained that
  queue, so the continuation that swaps the compiled source into the proxy
  never ran. Cross-thread resolvers (the async I/O worker, the tempo
  scheduler) and each REPL/cell evaluation now drain ready continuations
  when no evaluation is on the stack to do it.
- A call in return position inside an `async fn` -- `return f(x);`, or a
  trailing `f(x)` -- was compiled as a tail call, which replaced the async
  frame; when `f` returned it jumped to that frame's null return address
  and the process crashed. Tail calls are no longer emitted inside async
  fns (their result must go through the Future). In the app this was
  `drone play` evaluated a second time while already playing: the "adjust
  the volume" branch of `play` is exactly such a return.
- `play(proxy, events)` issued while the proxy's definition was still
  compiling -- Cookbook §12.4 run as a single cell -- read an empty
  parameter list, warned that the proxy had no noteParams, and then played
  every note at the spec defaults (all 440 Hz) once the sound existed. The
  player now parks the sequence and derives its Voice the moment the
  definition lands, so the pattern starts as written; `replace` and
  `enqueue` during that window park likewise.
- A synthdef could delete its own freshly built dylib before loading it
  (reproduced locally as a silent live-coding session: `live: anchor def
  failed: ... failed to load plugin`; the likely cause of one reported on
  the forum). The
  build revision counter was seeded from disk once per process; if another
  tzpl process (a second app instance, a CLI render, a test run) sharing
  `~/tzpl-build` had since written higher-numbered revisions of the same
  def, the new build was numbered below them and the keep-three-newest
  prune removed it a moment before `dlopen`. The counter is now re-synced
  with the disk before every build and the prune never touches the
  revision just built.
- The source tree failed to compile on Fedora (reported by a user): a
  newer libstdc++ no longer pulls in `<climits>` or `<unistd.h>`
  transitively, so `INT_MAX` in the type checker's "did you mean" typo
  suggestions and `usleep` in the NRT `_sleepMs` bridge were undeclared.
  The missing `<climits>` includes are added, and `usleep` is replaced
  with the standard `std::this_thread::sleep_for`.

## v0.2.4 (13 September 2026)

**Fixed**

- The macOS builds shipped in v0.2.0 through v0.2.3 required macOS 15.6
  (the machine they were built on) rather than the intended macOS 13.3
  floor, so the app refused to launch on Ventura, Sonoma, and early
  Sequoia. The 13.3 pin in `CMakeLists.txt` was guarded by "unless already
  set", and a build directory configured before the pin existed had the
  host's version cached, which silently won. The deployment target is now
  forced from `TZPL_MACOS_DEPLOYMENT_TARGET` (default 13.3) on every
  configure, and `make_dist_dmg.sh` refuses to package a binary whose
  Mach-O `minos` is above the configured floor. The DMG is Apple Silicon
  only, as before.

## v0.2.3 (13 September 2026)

**Fixed**

- A distribution example's imports of its sibling modules failed in the
  app: opening `examples/sc_conversions/sc2_examples_1.x` and running it
  stopped at `import sc2_common.*;` with "Cannot find module". Examples
  open as unsaved copies so an edit is never saved back into the
  distribution folder, and a copy had no path for document-relative
  imports to anchor to. A copy (editor tab or detached notebook) now
  anchors to the example it was opened from, while Save still asks for a
  location. The CLI had the same gap for a bare filename: `tzpl foo.x` run
  from inside foo's directory skipped the sibling lookup because the path
  had no directory component; it is made absolute first now.
- A plain lambda written inside a `coro fn` lost its trailing-expression
  return since the v0.2.2 anonymous-coroutine fix: the "no implicit return
  in a coroutine body" rule keyed on the enclosing function rather than
  the lambda itself, so every `fn() T { expr }` nested in a coroutine
  returned nothing. A live session's `define(px, fn() S { ... })` graph
  functions hit this -- the defs rendered silence and a voicer def
  reported no noteParams. `async fn` lambdas already keyed on their own
  flag; coroutine lambdas now do too.
- The C++ synthdef compiler silently dropped a delay write that lived only
  inside a bare `if_` (`if_(trig, fn(){ y <- white(1) })`, the lfnoise
  shape): the control-flow node had no consumers, so `findGraphCuts` marked
  it Unused and dead-code removal swept the whole branch, ring buffer
  write included. The synthc port had the fix; the C++ side now keeps such
  a node as a ControlFlow cut, declares its phi result up front (it was
  assigned but never declared), advances a ring buffer's write head where
  the write runs rather than where the buffer was declared (so a
  conditionally written buffer holds the last N written values, as the
  lfnoise generators need), and shares delay lines fed the same signal the
  way synthc always has (its merge pass had keyed on the writer node and
  never fired). The `nestedDelayVoicer` codegen validity case, left failing
  on purpose, now passes byte parity on both compilers.
- Building from source on Linux failed since v0.2.2 with "redefinition of
  'ec'" in `synthdef_compile_link.cpp`: the Linux-only Sleef staging block
  redeclared a variable that the v0.2.2 error-handling rework had hoisted
  to function scope. The block now shares that variable and reports a
  failed Sleef header or library copy as a proper error instead of
  ignoring it. The Linux CI jobs are now required, so a Linux-only build
  break fails the workflow instead of hiding behind a green badge
  (issue #11).
- `play` on a def whose `defSynth`/`defSynthX` is still compiling waits for
  it instead of creating a node against a def that does not exist yet
  (silence, plus a bare "errNodeDefNotFound" in the console). The compile
  FFI tracks in-flight compiles by def name and `play` awaits the one for
  its def, if any; `synthDefReady(name)` exposes the same future. A
  `newNode` for a def that is not loaded at all now says so by name, with
  the usual cause.
- An anonymous coroutine (`coro fn() Float { ... }()`) whose body ended in
  a trailing expression, if-else, or match crashed the process when it
  finished -- so a `go` task that ran to completion took the app down with
  it. The lambda code path compiled the trailing expression as a return,
  which popped the coroutine's frame and jumped through its null return
  address; it now falls through to the coroutine's done path, as a named
  `coro fn` always did.

## v0.2.2 (7 September 2026)

**Fixed**

- Widget values set from Tzopilotl code (`setValue`, `setNotes`, a fresh
  `control(node, ...)` binding) reached the engine only after the next
  click or drag in the app. The dispatcher that forwards widget values
  stops its timer when idle and was woken only by GUI events, so a
  sequencer coroutine driving a synth through its control widgets stayed
  silent until the user touched something. Code-side changes now wake
  it.
- The examples await their synthdef compiles. `ui_piano_roll.x` played
  before its def existed and was silent, with "errNodeDefNotFound" in
  the console; `instrument_synthdefs.x`, `effect_synthdefs.x`, and
  `note_synthdefs.x` returned from import while their defs were still
  compiling, so `instrumentTest.x` lost its first instrument the same
  way. Every `defSynthX` in those files now carries the `await` the docs
  prescribe; a single worker compiles the defs in order either way, so
  nothing gets slower.
- Headless `--wait` now waits. It kept the process alive only while a
  message listener was active, so a `--nogui --wait` script driving the
  tempo scheduler exited after the first scheduled callback.
- The release app crashed the moment a synth definition was compiled on
  any machine other than the one it was built on. Compiling a synth stages
  the plugin headers (`tzpl_plugin_abi.h` and friends) into `~/tzpl-build`,
  and the release build took them from the build machine's source tree;
  when that path did not exist the filesystem error escaped the compile
  worker thread and aborted the process. The distribution folder now ships
  those headers in `include/`, found relative to the app like `modules/`
  (or via `TZPL_HOME`), and a missing header directory is reported as a
  compile error for that synthdef instead of a crash.
- Inline REPL results are boxed correctly: a `Complex` value no longer
  prints as nil, and a `Fraction` value no longer crashes the REPL.

**App**

- Find in Files (Cmd+Shift+F): the sidebar swaps its file tree for a Find
  face, Xcode-style, listing every match across the open folders' documents
  and the editor tabs (unsaved text included), grouped by file with the
  matched span highlighted. Clicking a hit opens the file and selects it;
  Escape returns to the file tree. The sidebar column now has Files / Find
  tabs.
- Find Definitions (Cmd+Shift+J): every definition of the name at the
  caret that is in scope -- enclosing blocks first, then the file's top
  level, then the modules it imports (following `export` re-exports and
  `as` aliases; `mod.name` looks only in `mod`). Lists overloads,
  parameters, `let`/`var` bindings, enum cases, and type declarations. A
  name with no Tzopilotl source is named for what it is: a built-in
  function, or a foreign function of the bridge's native module.
- FFI Guide, Audio Engine: a new Buffers & Sample Banks subsection
  (`resizeBuffer`, `loadBuffer`, `fillBuffer`, `loadSampleBank`,
  `sampleZone`), the shared-input functions, the `TapMode` enum and the
  missing `Err` cases, and a function index listing every `audio_engine`
  function with its section and thread class.
- The Find bar and Find in Files share a match-mode popup: Contains,
  Matches Word, Starts With, Ends With, and Regular Expression (with
  `$1`-style groups in replacements), each with the case toggle.
- Sidebar roots that share a name are told apart: each shows the shortest
  run of parent directories that differs, dimmed after the name
  ("modules  .../tzpl_1/tzpl/lang" next to "modules  .../tzpl_2/tzpl/lang"),
  so a source tree's stdlib and an installed distribution's no longer look
  identical.

## v0.2.1 (4 September 2026)

**App**

- Distribution examples open cleaner: an unedited example copy no longer
  prompts to save when the app closes, and clicking an already-open example
  in the sidebar switches to its tab instead of opening another copy.
  Editing a copy marks it modified (asterisk) and prompts on close as
  usual, and Save still asks for a location outside the distribution
  folder.

## v0.2.0 (4 September 2026)

**Language & VM**

- `clear!` builtin empties an Array, Map, or Set in place; `append!`
  bulk-appends an array or list to an array in place (the mutating analogue
  of `$`); `isEmpty` answers emptiness for every collection `length` covers
  (arrays, lists, maps, sets, strings, ranges, persistent vectors/maps).
  `isEmpty` on a `List` is O(1) and safe on infinite lists.
- Stdlib modules (`music.play`, `music.spans`, `live.proxy`, `std.strings`,
  `synthc`) rewritten to use `filter`, auto-mapping, `drop`, `clear!`,
  `append!`, and `isEmpty` in place of manual loops.

**Libraries & Examples**

- `music.job` gains `realizeCo`, an incremental realize in the HMSL-player
  style: the hierarchy is walked lazily as the player pulls events, so
  selection decisions are made just-in-time (one event ahead of playback)
  and handing a huge form to `play` costs tree depth up front, not the
  whole form.

**Platform**

- Linux support: the interpreter, synthdef compiler (including runtime
  plugin compilation to `.so`), audio engine (ALSA/JACK/PulseAudio via
  RtAudio auto-selection), bridges, headless `tzpl_app`, and the JUCE app
  now build and run on Linux with Clang 19+. `docs/LINUX.md` covers
  requirements, the Docker dev environment (`dev/linux/Dockerfile`), and
  real-time configuration. The Dear ImGui GUI remains macOS-only; on Linux
  the JUCE app is the GUI, and it now also feeds the `mouseX` / `mouseY` /
  `mouseButton` ugens there.

## v0.1.0 (3 September 2026)

**Language & VM**

- `panic` builtin, and error halts that actually halt -- `defSynth` /
  `defSynthX` now fail loud instead of continuing on a broken graph.
- `Int / Int` fuses into a single `DIV_INT_TO_FRAC` opcode, speeding up
  Fraction-producing division.
- Module initializers run even when the importing REPL evaluation fails.

**SynthDefs & UGens**

- Integer bit operations work end-to-end in synthdefs, with indexed delay
  state and an in-place `put` peephole optimization.
- The pink-noise family: SuperCollider-style `pink` (plus `blue`, `violet`,
  `red`, `gray`, `white` refinements) with corrected Kellett filter state
  reads.
- `d(0)` resolves to the written signal -- a delay of zero, as expected.
- Voicer fixes: non-power-of-two voice counts no longer silence voicers;
  per-voice shape is kept through `PhiNode` with a voicer target;
  C++/synthc compiler parity on the `instruments.x` voicers.
- `fadeout` ugen -- the fixed-time companion to `fadein`.

**Engine & Control**

- `setControl` by control *name* (not just index) across the engine, OSC,
  and NATS interfaces.
- NRT renders honor `setTempo`; UI bindings inside renders stay quiet.

**Libraries & Examples**

- `instruments.x`: a note-playing instrument synthdef library
  (Karplus-Strong pluck, wavetable lead, resonator bank, sample players),
  documented in a new Music Cookbook chapter.
- `examples/music_fx_demos.x`: three multi-section demos tying the music
  dialects, instruments, and effects libraries together in one persistent
  node graph -- also rendered in the site gallery.

**App**

- Manageable panel windows: sizing, tiling, tabs, and Cmd+` cycling.
- `.x` files opened from Finder open in the editor, not the notebook.

**Project**

- Open-source release housekeeping: GPLv3 licensing, contribution and
  security policies, third-party notices.
- The documentation site (this site): landing page, unified shell, ⌘K
  search, per-chapter guide pages, and the audio gallery.
