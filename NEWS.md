# TZPL News

Curated highlights of what has changed, newest first. The full history is
in [git](https://github.com/lfnoise/tzpl/commits/main); this file records
the changes worth knowing about as a user of the platform. Rendered on the
documentation site as the Changelog page.

## Unreleased

Nothing yet.

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
