-- Tier-3 A/B voicer12 half (synthc): the identical 12-voice Karplus-Strong
-- pluck compiled by synthc (defSynthX). Must render bit-identically to
-- synthc_render_ab_voicer12_ref.x. See that file and run_synthc_render_ab.sh.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import instruments.*;
import audio_engine.*;

ksPluck(12) defSynthX("ab_voicer12") await;

begin();
newNode("ab_voicer12", 100);
connect(100, 0, 0, 0);
sched(0);
begin(); noteOn(100, 1, [220.0, 0.9]); sched(0);
begin(); noteOn(100, 2, [330.0, 0.8]); sched(0, 0, 0.03);
begin(); noteOn(100, 3, [440.0, 0.7]); sched(0, 0, 0.06);
