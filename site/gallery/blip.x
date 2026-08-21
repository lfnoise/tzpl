-- Gallery clip: Blip. Rendered offline by site/render_gallery.py.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine as ae;
import clock.*;

-- gallery-excerpt-begin
-- A band-limited impulse pair whose harmonic count sweeps with a slow
-- sine, fading in over a tenth of a second.
fn blipTest() S {
    let h = 1/20 sinosc(0.75) bilin(1, 48);
    [36, 36.13] blip(0, h) fadein(0.1) * 0.2 |> outlet
}
-- gallery-excerpt-end

blipTest defSynthX("blipTest") await;

go(coro fn() Float {
    "blipTest" playFor(20.0) yieldAll;
    ae.endRender(0.2);
}());
