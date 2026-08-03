-- Buffer wrap-at-length test (12-frame half): the same ramp stored twice in
-- a 12-frame buffer, read by the same counter. Reading index i from the
-- 6-frame ramp equals reading i from this buffer for every i exactly when
-- both wrap at their true lengths.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn wrapRead() S {
	let b = bufferVar();
	let t = delayVar();
	t <- t(1) + 1.0;
	b vread(t(1), Interpolation.none) f32 outlet
}

defSynthX(wrapRead, "fbWrap12");
begin();
newNode("fbWrap12", 100);
fillBuffer(100, 0, 1, [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6]);
connect(100, 0, 0, 0);
sched(0);
