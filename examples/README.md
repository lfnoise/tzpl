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
  `lang/scratch/instrumentTest.x` plays a phrase through each one.

The full guide is `lang/docs/Live_Controls_and_Notebooks.html`.
