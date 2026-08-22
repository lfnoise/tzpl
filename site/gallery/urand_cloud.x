-- Gallery clip: Sine Cloud. Rendered offline by site/render_gallery.py.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine as ae;
import clock.*;

-- gallery-excerpt-begin
-- Sixteen sines at init-time random frequencies, each amplitude-modulated
-- by its own slow random LFO, mixed down to stereo. Auto-mapping builds
-- all sixteen channels from array-shaped arguments.
fn sineCloud() S {
    let chans = 16;
    let detune = [-1, 1] vec;
    let freqs = exprand(100, 600, 8, Rate.init) + detune;
    let ampPhases = urand(chans, Rate.init);
    let ampFreqs = exprand(0.1, 0.5, chans, Rate.init);
    let amps = ampFreqs fsinosc(ampPhases) uni;
    let oscs = freqs fsinosc cb * amps;
    oscs sum(2) * 0.1 |> outlet
}
-- gallery-excerpt-end

sineCloud defSynthX("sineCloud") await;

go(coro fn() Float {
    "sineCloud" playFor(16.0) yieldAll;
    ae.endRender(0.2);
}());
