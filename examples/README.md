# Examples

Small runnable demos of the platform. Open a file in the app and
evaluate it (Cmd+Return), or paste it into a notebook code cell.

- `ui_widgets.x` -- tour of the `ui` module's input widgets: sliders
  (ControlSpec and linear-sugar forms), number, toggle, button, xy pad,
  multislider, matrix, piano roll, label; panels and onChange callbacks.
  Needs no audio.
- `ui_synth_controls.x` -- the Tangible-Values move: compile a synthdef
  whose controls carry ControlSpecs, play it, and let `controls(node)`
  materialize its whole interface as engine-bound widgets, plus a level
  meter and scope on its output. Run with audio on.
- `ui_piano_roll.x` -- a pianoRoll driving a synth: a clock coroutine
  steps the roll in 16ths and plays it through the synth's materialized
  control widgets, re-reading the notes live so edits are heard on the
  next pass. Set `edo` to 19/31/53 for those equal temperaments (pitch
  is in steps of 1/edo octave). Run with audio on.
- `example_synthdefs.x` -- a corpus of synthdefs exercising the ugen
  library (oscillators, noise colors, pause/pull graphs, delays, verbs),
  plus `playExamples()` / `renderExamples()` to audition them all in
  sequence. Run with audio on.
- `instrument_synthdefs.x` -- ten voicer-based polyphonic instruments
  (organ, FM bell/brass, saw lead, PWM pad, Karplus-Strong pluck, modal
  bell, kick, snare, sub bass), each triggered with `noteOn`/`noteOff`.
  `instrumentTest.x` plays a phrase through each one.
- `instrumentTest.x` -- plays a short arpeggio phrase through every
  instrument in `instrument_synthdefs.x` in sequence. Run with audio on
  (needs `examples/` on the module path).
- `sc_conversions/` -- Tzopilotl translations of the SuperCollider 2
  example corpus in `sc-examples/SC2-examples/` (`examples-1.txt` through
  `examples-12.txt`), one file per source file (`sc2_examples_1.x` ...
  `sc2_examples_12.x`; ~140 examples). Each file's header comment tracks
  which of its source examples are ported and how, and notes the few that
  are skipped (they need MIDI input or sound files). Audio-input examples
  (file 5) come in a `...Live` variant on the audio input and a `...Demo`
  variant on a synthetic source.
  `sc2_common.x` holds the shared machinery: SC's `Spawn`/`OverlapTexture`/
  `XFadeTexture` become a voicer plus a script-driven spawn loop (the
  timing model is documented there), and SC2 UGens with no Tzopilotl
  counterpart (Klank, Klang, Resonz, Crackle) are built from `ring`,
  `fsinxosc`, `bpf`. Each file ends with a `playAllScN()` / `renderScN()`
  driver. Run with audio on (needs `examples/` and `examples/sc_conversions/`
  on the module path).
- `rosetta.x` -- programming examples from Rosetta Code written in
  idiomatic Tzopilotl: pipeline syntax, auto-mapping, lazy lists, no
  explicit loops. Needs no audio.
- `cookbook/` -- the Tzopilotl Music Cookbook as notebooks, one `.tzd`
  per chapter (2-11): every example from
  `lang/docs/Tzopilotl_Music_Cookbook.html` as a runnable code cell,
  with the chapter's text as prose cells. Nothing runs on open;
  run cells with their Run button. Generated from the cookbook
  HTML via `std.notebook`.

The full guide is `lang/docs/Live_Controls_and_Notebooks.html`.
