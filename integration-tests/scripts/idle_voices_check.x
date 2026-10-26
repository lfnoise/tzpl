-- Idle voices (block engine plan M9): a released voice is skipped once it
-- is done (doneWhen) or, declaring nothing, once its output has stayed below
-- -120 dB for the voicer's silence time.
--
-- run_idle_voices_test.sh compiles these with SYNTHC_CORPUS_MODE=compile,
-- once normally and once with TZPL_FORMS=noidle, and `forms_equiv --idle`
-- plays voice 1 from 0 s, releases it at 0.5 s and plays it again at 1.5 s.
-- run_idle_voices_test.sh holds when each output must become exactly 0.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

fn env() S = linen(gate(), 0.01, 0.05);

-- Every voice adds 1e-7 (below -120 dB, so still silent): the output is
-- exactly 0 only once every voice is idle.
fn q(x S) S = x + 1e-7;

var cases [(String, GraphFn)] = [];

-- idle 0.2 s after the envelope reaches 0
cases push!(("idle_decay", fn() S { voicer(8, fn() S { sinosc(440) * env() |> q }) sum |> outlet }));
-- two channels per voice (SIMD groups of 2 voices)
cases push!(("idle_stereo", fn() S {
	voicer(4, fn() S { [sinosc(440), sinosc(660)] stack * env() |> q }) sum(0) |> outlet
}));
-- control flow in the body: per-voice structs (AoS)
cases push!(("idle_aos", fn() S {
	voicer(8, fn() S { let e = env(); (e > 0) pause(fn() S { sinosc(440) * e }) |> q }) sum |> outlet
}));
-- declared: done as soon as the envelope is 0
cases push!(("idle_done", fn() S {
	voicer(8, fn() S { let e = env(); sinosc(440) * e |> q doneWhen(e <= 0) }) sum |> outlet
}));
-- a longer silence time
cases push!(("idle_long", fn() S { voicer(8, fn() S { sinosc(440) * env() |> q }, 0.5) sum |> outlet }));
-- silence 0: no skipping at all
cases push!(("idle_off", fn() S { voicer(8, fn() S { sinosc(440) * env() |> q }, 0.0) sum |> outlet }));
cases push!(("idle_offdone", fn() S {
	voicer(8, fn() S { let e = env(); sinosc(440) * e |> q doneWhen(e <= 0) }, 0.0) sum |> outlet
}));
-- a 4 Hz square tremolo, no envelope: its 0.125 s gaps do not end a voice
cases push!(("idle_trem", fn() S { voicer(8, fn() S { sinosc(440) * (sinosc(4) > 0) |> q }) sum |> outlet }));

dumpCorpus(cases);
