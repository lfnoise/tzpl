-- Tier-3 A/B voicer12 half (reference): a NON-power-of-two voicer (12-voice
-- Karplus-Strong pluck) compiled with the C++ compiler (defSynth), with three
-- overlapping notes. Rendered to WAV by `tzpl_app --nrt`; the synthc half
-- builds the identical graph with defSynthX and the two renders must be
-- bit-identical. See run_synthc_render_ab.sh (rendered with TZPL_RNG_SEED so
-- both halves draw the same noise stream).
--
-- Guards the flat-voice phi copy loop's indexing: with 12 voices a
-- `& (voices-1)` wrap mask scrambled/skipped voices and both compilers agreed
-- on the broken bytes, so only a behavioral non-pow2 render catches it (the
-- comparator asserts the reference is non-silent).
import synthdef.*;
import common_ugens.*;
import instruments.*;
import audio_engine.*;

ksPluck(12) defSynth("ab_voicer12") await;

begin();
newNode("ab_voicer12", 100);
connect(100, 0, 0, 0);
sched(0);
begin(); noteOn(100, 1, [220.0, 0.9]); sched(0);
begin(); noteOn(100, 2, [330.0, 0.8]); sched(0, 0, 0.03);
begin(); noteOn(100, 3, [440.0, 0.7]); sched(0, 0, 0.06);
