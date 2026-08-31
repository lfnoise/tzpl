-- Gated linen envelope (common_ugens.x) shape check. Two envelopes rendered
-- as DC, one per channel:
--   ch 0 -- full release: gate high for 1 s, so the 0.2 s rise completes,
--           holds at exactly 1, then decays to 0 over 0.4 s.
--   ch 1 -- early release: gate drops at 0.1 s, halfway up the rise; the
--           decay must start immediately from the current amplitude (~0.5)
--           with the same full-scale slope (1/dec per second).
-- Rendered by `tzpl_app --nrt`; the checker in run_linen_test.sh asserts
-- the slopes, the hold level, the early-release peak, and the zero tails.
import synthdef.*;
import common_ugens.*;
import audio_engine.*;

fn linenShapes() S {
	let t = delayVar();
	t <- t(1) + 1 / fs();
	let full = linen(t(0) < 1.0, 0.2, 0.4);
	let early = linen(t(0) < 0.1, 0.2, 0.4);
	[full, early] join outlet
}
linenShapes defSynth("linen_shapes");

safetyLimiter(false);
begin();
newNode("linen_shapes", 100);
connect(100, 0, 0, 0);
sched(0);
