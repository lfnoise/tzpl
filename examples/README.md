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

The full guide is `lang/docs/Live_Controls_and_Notebooks.html`.
