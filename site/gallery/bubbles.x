-- Gallery clip: Bubbles. Rendered offline by site/render_gallery.py.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine as ae;
import clock.*;

-- gallery-excerpt-begin
-- An LFO-driven sine through a comb filter. `nnhz` converts a MIDI-style
-- note number to Hz; `|>` pipes the left value in as the last argument.
fn bubbles() S =
    0.4 lfsaw * 24
    + [8, 7.23] lfsaw * 3
    + 81
    |> nnhz sinosc * 0.04
    |> combn(0.2, 4) outlet;
-- gallery-excerpt-end

bubbles defSynthX("bubbles") await;

go(coro fn() Float {
    "bubbles" playFor(16.0) yieldAll;
    ae.endRender(0.2);
}());
