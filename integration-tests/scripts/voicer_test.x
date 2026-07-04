-- voicer_test: Define a sine+envelope synthdef via the voicer codegen,
--              compile & load it, then play notes and chords.

import synthdef.*;
import common_ugens.*;
import clock.*;
import audio_engine.*;

println("--- voicer_test ----------------------------");

---------------------------------------------------------------------------
-- 1. Define a simple sine synth with an attack/release envelope.
--
--    noteParam layout (gate is always index 0, injected automatically):
--      0: gate      (auto)
--      1: pitch     (MIDI note number)
--      2: amp       (linear amplitude)
--      3: pan       (-1 .. +1)

fn sineVoice() S {
    let g   = gate();
    let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
    let amp = noteParam("amp",   ControlSpec { lo: 0.0, hi: 1.0,   init: 0.5,  warp: ControlWarp.linear });

    -- simple attack/release envelope driven by gate
    let env = g fadein(0.01) lag(0.3);

    -- oscillator: mono sine voice
    pch nnhz sinosc cb * env * amp
}

fn sineSynth() S = voicer(32, sineVoice) sum outlet;

sineSynth defSynth("sine_voice");

println("compiled sine_voice");
println(listSynthDefs());

---------------------------------------------------------------------------
-- 2. Start the engine and instantiate the voicer node.

let nodeID = 200;

go(coro fn() Float {
    engineStart();
    yield 0.5;

    begin();
    newNode("sine_voice", nodeID);
    connect(nodeID, 0, 0, 0);
    sched(0);
    yield 0.5;

    ---------------------------------------------------------------------------
    -- 3. Single notes -- ascending scale

    println("single notes");
    var nid = 0;
    let scale = [60.0, 62.0, 64.0, 65.0, 67.0, 69.0, 71.0, 72.0];
    let latency = 0.02;

    for (i : (0..7)) {
        let t = getStreamTime();
        begin();
        noteOn(nodeID, nid, [scale[i], 0.4]);
        sched(0, 0, t + latency);

        begin();
        noteOff(nodeID, nid);
        sched(0, 0, t + latency + 0.35);

        nid = nid + 1;
        yield 0.4;
    }
    yield 0.5;

    ---------------------------------------------------------------------------
    -- 4. Chords -- triads moving up

    println("chords");

    let roots    = [48.0, 50.0, 52.0, 53.0, 55.0, 57.0, 59.0, 60.0];
    let thirds   = [4.0,  3.0,  3.0,  4.0,  4.0,  3.0,  3.0,  4.0];  -- major / minor
    let fifths   = [7.0,  7.0,  7.0,  7.0,  7.0,  7.0,  6.0,  7.0];

    for (i : (0..7)) {
        let t = getStreamTime();
        let root = roots[i];

        -- three note chord
        begin();
        noteOn(nodeID, nid,     [root,              0.35]);
        noteOn(nodeID, nid + 1, [root + thirds[i],  0.35]);
        noteOn(nodeID, nid + 2, [root + fifths[i],  0.35]);
        sched(0, 0, t + latency);

        begin();
        noteOff(nodeID, nid);
        noteOff(nodeID, nid + 1);
        noteOff(nodeID, nid + 2);
        sched(0, 0, t + latency + 0.6);

        nid = nid + 3;
        yield 0.7;
    }
    yield 0.5;

    ---------------------------------------------------------------------------
    -- 5. Rapid arpeggiated chord

    println("arpeggio");

    let arpNotes = [48.0, 55.0, 60.0, 64.0, 67.0, 72.0];
    let t0 = getStreamTime();

    for (i : (0..5)) {
        begin();
        noteOn(nodeID, nid, [arpNotes[i], 0.3]);
        sched(0, 0, t0 + latency + 0.06 * i);

        begin();
        noteOff(nodeID, nid);
        sched(0, 0, t0 + latency + 0.06 * i + 1.0);

        nid = nid + 1;
    }
    yield 1.5;

    ---------------------------------------------------------------------------
    -- 6. Polyphony stress -- many simultaneous voices

    println("polyphony stress");

    for (k : (0..2)) {
        let t = getStreamTime();
        for (i : (0..7)) {
            let pitch = 48.0 + 3.0 * i + 12.0 * k;
            begin();
            noteOn(nodeID, nid, [pitch, 0.2]);
            sched(0, 0, t + latency);
            nid = nid + 1;
        }

        -- release after 0.8s
        begin();
        for (j : (0..7)) {
            noteOff(nodeID, nid - 8 + j);
        }
        sched(0, 0, t + latency + 0.8);
        yield 1.0;
    }
    yield 1.0;

    ---------------------------------------------------------------------------
    -- 7. noteSetParams -- glissando on a held note

    println("glissando");

    let tGliss = getStreamTime();
    begin();
    noteOn(nodeID, nid, [60.0, 0.4]);
    sched(0, 0, tGliss + latency);

    for (i : (1..24)) {
        begin();
        noteSetParams(nodeID, nid, 0, [60.0 + 0.5 * i]);
        sched(0, 0, tGliss + latency + 0.05 * i);
    }

    begin();
    noteOff(nodeID, nid);
    sched(0, 0, tGliss + latency + 0.05 * 25);
    nid = nid + 1;
    yield 2.0;

    ---------------------------------------------------------------------------
    -- 8. random cloud

    println("random cloud");
    let tCloud = getStreamTime() + latency;
    for (i : (1..100)) {
        let t = tCloud + 0.25 * (i + std.urand());
        begin();
        noteOn(nodeID, nid, [std.rand(48, 84), 0.4]);
        sched(0, 0, t);

        begin();
        noteOff(nodeID, nid);
        sched(0, 0, t + 0.25 * std.rand(1.0, 8.0));
        nid = nid + 1;
    }
    yield 28.0;

    ---------------------------------------------------------------------------
    -- 9. allNotesOff

    println("all notes off");
    begin();
    allNotesOff(nodeID);
    sched(0);
    yield 1.0;

    ---------------------------------------------------------------------------
    -- 10. Cleanup

    println("stop");
    engineStop();
    println("voicer_test done");
}());
