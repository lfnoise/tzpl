-- Buffer wrap-at-length test (6-frame half): a deliberately non-power-of-two
-- 6-frame ramp buffer read with an ever-growing sample counter. With
-- wrap-at-length semantics the output cycles through all six ramp values;
-- under the old power-of-two mask it would have read zero padding at
-- indices 6..7 and wrapped at 8. Must render bit-identically to
-- fill_buffer_wrap12.x (a 12-frame buffer holding the ramp twice).
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

defSynthX(wrapRead, "fbWrap6");
begin();
newNode("fbWrap6", 100);
fillBuffer(100, 0, 1, [0.1, 0.2, 0.3, 0.4, 0.5, 0.6]);
connect(100, 0, 0, 0);
sched(0);
