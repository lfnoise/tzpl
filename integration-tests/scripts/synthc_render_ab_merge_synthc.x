-- Tier-3 A/B merge half (synthc): the SAME two-delay synth compiled by synthc
-- (defSynthX). synthc keys delay allocation on the written signal, so the two
-- identical lines collapse to ONE ring buffer with two read taps. The rendered
-- audio must be bit-identical to the C++ two-buffer reference -- a content-
-- preserving optimization.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;

fn mergesyn() S {
	let sig = (fs() / 220.0) sin * 0.001;
	let a = delayVar(); a init(4, 0.0);
	let b = delayVar(); b init(4, 0.0);
	a write(sig); b write(sig);
	(a read(1) * 0.6 + b read(4) * 0.4) outlet
}
defSynthX(mergesyn, "ab_merge");

begin(0);
newNode("ab_merge", 100);
connect(100, 0, 0, 0);
sched();
