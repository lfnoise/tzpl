-- Gallery clip: Fraction Sequencer. Rendered offline by site/render_gallery.py.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine as ae;
import clock.*;

-- gallery-excerpt-begin
-- Three detuned smooth-saw layers step through a just-intonation pattern
-- (frequency ratios as exact Fractions) at three tempos, into a comb echo.
fn seqTest() S {
    let middleC = 256;
    let pattern = [1/1, 6/5, 3/2, 9/5, 2/1, 12/5, 3, 18/5] * middleC;
    let detune = [-0.04, 0.04];
    (
          (1 lfimp seq(pattern * 0.25, 8) + detune) smoothSaw(5) * 1.4
        + (2 lfimp seq(pattern * 1.0,  8) + detune) smoothSaw(4)
        + (4 lfimp seq(pattern * 2.0,  8) + detune) smoothSaw(3) * 0.7
    ) combn(3/8, 4) * 0.2 |> outlet
}
-- gallery-excerpt-end

seqTest defSynthX("seqTest") await;

go(coro fn() Float {
    "seqTest" playFor(24.0) yieldAll;
    ae.endRender(0.5);
}());
