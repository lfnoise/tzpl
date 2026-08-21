# Tutorial 1: First Sounds

This is the first of a progressive series. Each tutorial assumes the app is
installed and running -- see [Download & Install](Downloads.html) -- and
builds on the ones before it. In this one you will define a synth, play it,
and change it while it plays.

## 1. Launch and evaluate

Open **Tzopilotl.app** and create a new file (or just use the scratch tab).
Evaluation is cursor-driven, like other live-coding environments:

| Shortcut | Action |
|----------|--------|
| Cmd+Enter | evaluate the selection, or the block under the cursor |
| Shift+Enter | evaluate the current line |
| Cmd+Shift+Enter | evaluate the whole file |
| Cmd+K | clear the console |

Type this line and press **Shift+Enter** on it:

```
"hello, tzopilotl" println;
```

The text lands in the console pane. That is the whole loop: put the cursor
on code, evaluate it, watch the console.

## 2. Define a synth

Paste this block and evaluate it (**Cmd+Shift+Enter** evaluates the whole
file):

```
import synthdef.*;
import synthc.compile.*;   -- defSynthX
import common_ugens.*;
import audio_engine as ae;

-- An LFO-driven sine through a comb filter. `nnhz` converts a MIDI-style
-- note number to Hz; `|>` pipes the left value in as the last argument.
fn bubbles() S =
    0.4 lfsaw * 24
    + [8, 7.23] lfsaw * 3
    + 81
    |> nnhz sinosc * 0.04
    |> combn(0.2, 4) outlet;

bubbles defSynthX("bubbles") await;
```

The console prints the compile and link commands, then:

```
bubbles compiled successfully (synthc).
```

`defSynthX` walked the signal graph your function returned, generated SIMD
C++ for it, compiled and linked a native plugin in the background, and
registered it with the running engine under the name `"bubbles"`. The
`await` makes the script wait for the compile because the next step plays
it. No sound yet -- you have defined an instrument, not played it.

## 3. Play it

```
let id = play("bubbles");
```

Sound. The synth is *free-running*: it plays continuously from the moment
its node is created. Stop it with:

```
stop(id);
```

## 4. Change it while it plays

Start it again (`let id2 = play("bubbles");`), then edit the definition --
change `combn(0.2, 4)` to `combn(0.4, 8)` for a slower, longer echo, or
`* 0.04` to `* 0.08` for louder -- and re-evaluate the `fn bubbles` block
*and* the `defSynthX` line (without the `await` this time). Compilation
runs in the background; the playing node hot-swaps to the new version the
moment it finishes loading. The music never pauses. This
define-listen-redefine loop is the core of live coding in Tzopilotl.

When you're done: `stop(id2);`

## 5. Where to go next

- **File > Open Example...** -- `example_synthdefs.x` is a corpus of synths
  with a `playExamples()` tour; `ui_synth_controls.x` grows a live control
  surface.
- The [Music Cookbook](Tzopilotl_Music_Cookbook.html) continues exactly
  where this tutorial stops: polyphonic voices, note parameters, envelopes,
  and the composition dialects.
- [Writing SynthDefs](Writing_SynthDefs.html) is the full guide to the
  signal-graph layer: rates, channels, delays, buffers, voicers.

*Further tutorials in this series are planned; the cookbook covers the
territory task-by-task in the meantime.*
