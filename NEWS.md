# TZPL News

Curated highlights of what has changed, newest first. The full history is
in [git](https://github.com/lfnoise/tzpl/commits/main); this file records
the changes worth knowing about as a user of the platform. Rendered on the
documentation site as the Changelog page.

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
