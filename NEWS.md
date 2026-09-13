# TZPL News

Curated highlights of what has changed, newest first. The full history is
in [git](https://github.com/lfnoise/tzpl/commits/main); this file records
the changes worth knowing about as a user of the platform. Rendered on the
documentation site as the Changelog page.

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
